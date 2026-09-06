// SPDX-License-Identifier: GPL-3.0-only
/*
 *  AuraCore - headless backend implementation
 *  Copyright (C) 2026 Aura Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "Backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>
#include <algorithm>
#include <cstring>

#include "auracore/backend.h"

#include "CoreApplication.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "java/JavaChecker.h"
#include "java/JavaUtils.h"
#include "meta/Index.h"
#include "meta/Version.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "InstanceDirUpdate.h"
#include "meta/VersionList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "net/Mode.h"
#include "tasks/Task.h"

namespace AuraCore {

namespace {

QByteArray toJson(const QJsonDocument& document)
{
    return document.toJson(QJsonDocument::Compact);
}

QJsonObject versionToJson(const Meta::Version::Ptr& version)
{
    return QJsonObject{
        { "version", version->version() },
        { "type", version->type() },
        { "releaseTime", version->time().toString(Qt::ISODate) },
        { "recommended", version->isRecommended() },
    };
}
QJsonObject instanceToJson(MinecraftInstance* instance, const QString& group)
{
    QJsonObject object;
    object.insert("id", instance->id());
    object.insert("name", instance->name());
    object.insert("dir", instance->instanceRoot());
    object.insert("icon", instance->iconKey());
    if (!group.isEmpty()) {
        object.insert("group", group);
    }
    object.insert("lastLaunch", double(instance->lastLaunch()));

    // Best effort: the component table may not have finished loading for
    // freshly discovered instances, in which case the field stays null.
    if (auto* profile = instance->getPackProfile()) {
        if (const auto& component = profile->getComponent("net.minecraft")) {
            object.insert("gameVersion", component->getVersion());
        }
    }
    return object;
}

}  // namespace

Backend::Backend(std::unique_ptr<CoreApplication> core) : m_core(std::move(core)) {}

Backend::~Backend() = default;

std::unique_ptr<Backend> Backend::create(const QString& dataPath, QString* error)
{
    if (QCoreApplication::instance() == nullptr) {
        if (error) {
            *error = QStringLiteral("A QCoreApplication must exist before creating the backend");
        }
        return nullptr;
    }

    auto core = std::make_unique<CoreApplication>(dataPath);
    if (!core->initialize()) {
        if (error) {
            *error = QStringLiteral("Failed to initialize the AuraCore data directory: %1").arg(dataPath);
        }
        return nullptr;
    }
    return std::unique_ptr<Backend>(new Backend(std::move(core)));
}

QByteArray Backend::listInstances()
{
    QJsonArray array;
    const auto* instances = m_core->instances();
    for (int i = 0; i < instances->count(); ++i) {
        array.append(instanceToJson(instances->at(i), instances->getInstanceGroup(instances->at(i)->id())));
    }
    return toJson(QJsonDocument(array));
}

QByteArray Backend::getInstance(const QString& id)
{
    const auto* instances = m_core->instances();
    for (int i = 0; i < instances->count(); ++i) {
        auto* instance = instances->at(i);
        if (instance->id() == id) {
            return toJson(QJsonDocument(instanceToJson(instance, instances->getInstanceGroup(instance->id()))));
        }
    }
    m_lastError = QStringLiteral("Unknown instance id: %1").arg(id);
    return {};
}

QByteArray Backend::detectJava()
{
    QJsonArray array;
    JavaUtils utils;
    for (const auto& path : utils.FindJavaPaths()) {
        // Raw scans may yield registry stubs or PATH candidates that do not
        // exist; the ABI only reports executables the host can actually spawn.
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            continue;
        }
        array.append(QJsonObject{ { "path", path } });
    }
    return toJson(QJsonDocument(array));
}

bool Backend::runTaskSync(const Task::Ptr& task, int valveMs)
{
    QEventLoop loop;
    bool succeeded = false;
    QObject::connect(task.get(), &Task::succeeded, &loop, [&succeeded] { succeeded = true; });
    QObject::connect(task.get(), &Task::failed, &loop, &QEventLoop::quit);
    QObject::connect(task.get(), &Task::aborted, &loop, &QEventLoop::quit);
    task->start();
    // Synchronous tasks complete during start(); entering exec() then would
    // wait forever because quit() fired before the loop began. Corrupt meta
    // caches also fall back to the network with endless retries, so the loop
    // carries a safety valve regardless.
    QTimer::singleShot(valveMs, &loop, &QEventLoop::quit);
    if (!task->isFinished()) {
        loop.exec();
    }
    return succeeded;
}
bool Backend::loadMetaCache()
{
    if (m_metaLoaded) {
        return true;
    }

    auto* index = m_core->metadataIndex();
    // loadTask(Offline) still falls back to the network when no cache file
    // exists; this ABI is strictly read-only and must never go online.
    const QString indexFile = QDir("meta").absoluteFilePath(index->localFilename());
    if (!QFile::exists(indexFile)) {
        return false;
    }
    auto task = index->loadTask(Net::Mode::Offline);
    if (!task) {
        m_metaLoaded = !index->lists().isEmpty();
        return m_metaLoaded;
    }

    // Index() seeds the well-known uid registry even without a cache file,
    // so the flag must reflect an actual index.json load, not list presence.
    m_metaLoaded = runTaskSync(task);
    return m_metaLoaded;
}

QByteArray Backend::listComponentLists()
{
    const bool cached = loadMetaCache();
    QJsonArray lists;
    const auto* index = m_core->metadataIndex();
    for (const auto& versionList : index->lists()) {
        QJsonArray versions;
        for (const auto& version : versionList->versions()) {
            versions.append(versionToJson(version));
        }
        lists.append(QJsonObject{
            { "uid", versionList->uid() },
            { "name", versionList->name() },
            { "versions", versions },
        });
    }
    return toJson(QJsonDocument(QJsonObject{ { "cached", cached }, { "lists", lists } }));
}

QByteArray Backend::probeJava()
{
    JavaUtils utils;
    QStringList candidates;
    QSet<QString> seen;
    for (const auto& path : utils.FindJavaPaths()) {
        // Raw scans may yield registry stubs or PATH candidates that do not
        // exist; only executables the host can actually spawn are probed.
        if (path.isEmpty() || !QFileInfo::exists(path) || seen.contains(path)) {
            continue;
        }
        seen.insert(path);
        candidates.append(path);
        // Keep the synchronous ABI bounded; large PATH scans are rare.
        if (candidates.size() >= 8) {
            break;
        }
    }

    QJsonArray array;
    for (int i = 0; i < candidates.size(); ++i) {
        JavaChecker checker(candidates.at(i), "", 128, 512, 64, i);
        QEventLoop loop;
        JavaChecker::Result result;
        QObject::connect(&checker, &JavaChecker::checkFinished, &loop, [&loop, &result](const JavaChecker::Result& payload) {
            result = payload;
            loop.quit();
        });
        QObject::connect(&checker, &Task::failed, &loop, &QEventLoop::quit);
        QObject::connect(&checker, &Task::aborted, &loop, &QEventLoop::quit);
        checker.start();
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);
        if (!checker.isFinished()) {
            loop.exec();
        }

        QJsonObject entry{ { "path", candidates.at(i) } };
        if (result.validity == JavaChecker::Result::Validity::Valid) {
            entry.insert("version", result.javaVersion.toString());
            entry.insert("vendor", result.javaVendor);
            entry.insert("arch", result.is_64bit ? "x64" : "x86");
        } else {
            entry.insert("version", QJsonValue());
            entry.insert("error", result.errorLog.isEmpty() ? result.outLog : result.errorLog);
        }
        array.append(entry);
    }
    return toJson(QJsonDocument(array));
}

QByteArray Backend::listComponentVersions(const QString& uid)
{
    auto* index = m_core->metadataIndex();
    // The uid registry only fills after index.json loads; the cache file is
    // the authoritative signal, so unknown-but-cached uids still resolve.
    const auto versionList = index->get(uid);
    const QString cacheFile = QDir("meta").absoluteFilePath(versionList->localFilename());
    if (!QFile::exists(cacheFile)) {
        return toJson(QJsonDocument(QJsonObject{
            { "uid", uid },
            { "cached", false },
            { "versions", QJsonArray() },
        }));
    }
    const bool loaded = runTaskSync(versionList->loadTask(Net::Mode::Offline));
    QJsonArray versions;
    for (const auto& version : versionList->versions()) {
        versions.append(versionToJson(version));
    }
    return toJson(QJsonDocument(QJsonObject{
        { "uid", uid },
        { "cached", loaded },
        { "versions", versions },
    }));
}
QByteArray Backend::refreshMetadata()
{
    auto* index = m_core->metadataIndex();
    const bool refreshed = runTaskSync(index->loadTask(Net::Mode::Online, true), 120000);
    if (refreshed) {
        m_metaLoaded = true;
    }
    QJsonObject object{
        { "refreshed", refreshed },
        { "lists", double(index->lists().size()) },
    };
    if (!refreshed) {
        object.insert("error", m_lastError.isEmpty() ? QStringLiteral("metadata refresh failed or timed out") : m_lastError);
    }
    return toJson(QJsonDocument(object));
}

QByteArray Backend::refreshComponent(const QString& uid)
{
    auto* index = m_core->metadataIndex();
    const auto versionList = index->get(uid);
    const bool refreshed = runTaskSync(versionList->loadTask(Net::Mode::Online, true), 120000);
    QJsonObject object{
        { "uid", uid },
        { "refreshed", refreshed },
        { "versions", double(versionList->versions().size()) },
    };
    if (!refreshed) {
        object.insert("error", m_lastError.isEmpty() ? QStringLiteral("component refresh failed or timed out") : m_lastError);
    }
    return toJson(QJsonDocument(object));
}
Backend::TrackedTaskPtr Backend::trackTask(const Task::Ptr& task, const QString& type)
{
    auto tracked = std::make_shared<TrackedTask>();
    tracked->task = task;
    tracked->type = type;

    QObject::connect(task.get(), &Task::succeeded, [tracked] {
        tracked->finished = true;
        tracked->succeeded = true;
    });
    QObject::connect(task.get(), &Task::failed, [tracked](const QString& reason) {
        tracked->finished = true;
        tracked->error = reason;
    });
    QObject::connect(task.get(), &Task::aborted, [tracked] {
        tracked->finished = true;
        tracked->aborted = true;
    });
    QObject::connect(task.get(), &Task::progress,
                     [tracked](qint64 current, qint64 total) {
                         tracked->progress = current;
                         tracked->progressTotal = total;
                     });
    QObject::connect(task.get(), &Task::status, [tracked](const QString& status) { tracked->status = status; });

    const QString id = QString::number(m_nextTaskId++);
    m_tasks.insert(id, tracked);
    pruneFinishedTasks();
    return tracked;
}

void Backend::pruneFinishedTasks()
{
    // Keep the bookkeeping bounded for long-lived host processes.
    while (m_tasks.size() > 32) {
        const auto oldest = std::min_element(m_tasks.keyValueBegin(), m_tasks.keyValueEnd(),
                                             [](const auto& a, const auto& b) { return a.first.toInt() < b.first.toInt(); });
        if (oldest == m_tasks.keyValueEnd() || !oldest->second->finished) {
            break;
        }
        m_tasks.erase(m_tasks.constFind(oldest->first));
    }
}

QByteArray Backend::createInstance(const QString& name, const QString& gameVersion, const QString& group)
{
    auto* index = m_core->metadataIndex();
    const auto versionList = index->get(QStringLiteral("net.minecraft"));
    if (versionList->versions().isEmpty()) {
        // First run convenience: fetch the version list on demand.
        runTaskSync(versionList->loadTask(Net::Mode::Online), 120000);
    }
    const auto version = versionList->getVersion(gameVersion);
    if (!version) {
        return toJson(QJsonDocument(QJsonObject{
            { "created", false },
            { "error", QStringLiteral("Unknown Minecraft version: %1").arg(gameVersion) },
        }));
    }

    auto* creation = new VanillaCreationTask(version);
    creation->setName(name);
    creation->setIcon(QStringLiteral("default"));
    if (!group.isEmpty()) {
        creation->setGroup(group);
    }

    Task* staging = m_core->instances()->wrapInstanceTask(creation);
    if (staging == nullptr) {
        delete creation;
        m_lastError = QStringLiteral("Could not stage the new instance");
        return {};
    }

    const auto tracked = trackTask(Task::Ptr(staging), QStringLiteral("create-instance"));
    QMetaObject::invokeMethod(staging, &Task::start, Qt::QueuedConnection);

    // trackTask consumed the counter immediately before returning; the ABI is
    // single threaded, so the previous value is the task id.
    const QString taskId = QString::number(m_nextTaskId - 1);
    return toJson(QJsonDocument(QJsonObject{
        { "created", true },
        { "taskId", taskId },
        { "name", name },
        { "gameVersion", version->version() },
    }));
}

QByteArray Backend::renameInstance(const QString& id, const QString& newName)
{
    auto* instances = m_core->instances();
    MinecraftInstance* instance = nullptr;
    for (int i = 0; i < instances->count(); ++i) {
        if (instances->at(i)->id() == id) {
            instance = instances->at(i);
            break;
        }
    }
    if (instance == nullptr) {
        m_lastError = QStringLiteral("Unknown instance id: %1").arg(id);
        return {};
    }
    if (newName.isEmpty()) {
        m_lastError = QStringLiteral("Instance name must not be empty");
        return {};
    }

    const QString oldName = instance->name();
    instance->setName(newName);
    const QString newRoot = askToUpdateInstanceDirName(instance, oldName, newName, nullptr);
    const bool dirRenamed = !newRoot.isEmpty();
    if (dirRenamed) {
        // The staged in-memory objects point at the old directory now.
        if (instances->loadList() != InstanceList::NoError) {
            m_lastError = QStringLiteral("Instance list reload failed after directory rename");
            return {};
        }
    }
    return toJson(QJsonDocument(QJsonObject{
        { "renamed", true },
        { "oldId", id },
        { "id", dirRenamed ? QFileInfo(newRoot).fileName() : id },
        { "name", newName },
        { "dirRenamed", dirRenamed },
    }));
}

QByteArray Backend::setInstanceGroup(const QString& id, const QString& group)
{
    auto* instances = m_core->instances();
    MinecraftInstance* instance = nullptr;
    for (int i = 0; i < instances->count(); ++i) {
        if (instances->at(i)->id() == id) {
            instance = instances->at(i);
            break;
        }
    }
    if (instance == nullptr) {
        m_lastError = QStringLiteral("Unknown instance id: %1").arg(id);
        return {};
    }
    instances->setInstanceGroup(id, group);
    return toJson(QJsonDocument(QJsonObject{
        { "ok", true },
        { "id", id },
        { "group", group },
    }));
}

QByteArray Backend::setInstanceIcon(const QString& id, const QString& iconKey)
{
    auto* instances = m_core->instances();
    MinecraftInstance* instance = nullptr;
    for (int i = 0; i < instances->count(); ++i) {
        if (instances->at(i)->id() == id) {
            instance = instances->at(i);
            break;
        }
    }
    if (instance == nullptr) {
        m_lastError = QStringLiteral("Unknown instance id: %1").arg(id);
        return {};
    }
    instance->setIconKey(iconKey);
    return toJson(QJsonDocument(QJsonObject{
        { "ok", true },
        { "id", id },
        { "icon", iconKey },
    }));
}

QByteArray Backend::deleteInstance(const QString& id)
{
    auto* instances = m_core->instances();
    bool found = false;
    for (int i = 0; i < instances->count(); ++i) {
        if (instances->at(i)->id() == id) {
            found = true;
            break;
        }
    }
    if (!found) {
        m_lastError = QStringLiteral("Unknown instance id: %1").arg(id);
        return {};
    }

    instances->deleteInstance(id);
    if (instances->loadList() != InstanceList::NoError) {
        m_lastError = QStringLiteral("Instance list reload failed after delete");
        return {};
    }
    for (int i = 0; i < instances->count(); ++i) {
        if (instances->at(i)->id() == id) {
            return toJson(QJsonDocument(QJsonObject{
                { "deleted", false },
                { "id", id },
                { "error", QStringLiteral("Instance directory delete did not complete") },
            }));
        }
    }
    return toJson(QJsonDocument(QJsonObject{
        { "deleted", true },
        { "id", id },
    }));
}
QByteArray Backend::taskStatus(const QString& taskId)
{
    const auto it = m_tasks.constFind(taskId);
    if (it == m_tasks.constEnd() || !it.value()) {
        m_lastError = QStringLiteral("Unknown task id: %1").arg(taskId);
        return {};
    }
    const auto& tracked = it.value();
    QString state = QStringLiteral("running");
    if (tracked->finished) {
        state = tracked->aborted ? QStringLiteral("aborted")
                : tracked->succeeded ? QStringLiteral("succeeded")
                                     : QStringLiteral("failed");
    }
    QJsonObject object{
        { "id", taskId },
        { "type", tracked->type },
        { "state", state },
        { "progress", double(tracked->progress) },
        { "total", double(tracked->progressTotal) },
        { "status", tracked->status },
    };
    if (tracked->finished) {
        object.insert("succeeded", tracked->succeeded);
    }
    if (!tracked->error.isEmpty()) {
        object.insert("error", tracked->error);
    }
    return toJson(QJsonDocument(object));
}

QByteArray Backend::waitTask(const QString& taskId, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (true) {
        const auto it = m_tasks.constFind(taskId);
        if (it == m_tasks.constEnd() || !it.value()) {
            m_lastError = QStringLiteral("Unknown task id: %1").arg(taskId);
            return {};
        }
        if (it.value()->finished) {
            break;
        }
        const int remaining = timeoutMs - int(timer.elapsed());
        if (remaining <= 0) {
            break;
        }
        QEventLoop loop;
        QTimer::singleShot(qMin(remaining, 1000), &loop, &QEventLoop::quit);
        loop.exec();
    }
    return taskStatus(taskId);
}

bool Backend::cancelTask(const QString& taskId)
{
    const auto it = m_tasks.constFind(taskId);
    if (it == m_tasks.constEnd() || !it.value() || !it.value()->task) {
        return false;
    }
    return it.value()->task->abort();
}
}  // namespace AuraCore

struct auracore_backend {
    std::unique_ptr<AuraCore::Backend> backend;
    QByteArray errorBuffer;
};

extern "C" {

int auracore_abi_version(void)
{
    return AURACORE_ABI_VERSION;
}

auracore_status auracore_backend_create(const char* data_path, auracore_backend** out_backend)
{
    if (data_path == nullptr || out_backend == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }

    QString error;
    auto backend = AuraCore::Backend::create(QString::fromUtf8(data_path), &error);
    if (!backend) {
        // Create failures have no handle yet, so only the status is returned;
        // the reason is logged by the host through QCoreApplication.
        return AURACORE_ERROR_BACKEND;
    }

    auto* handle = new (std::nothrow) auracore_backend();
    if (handle == nullptr) {
        return AURACORE_ERROR_OUT_OF_MEMORY;
    }
    handle->backend = std::move(backend);
    *out_backend = handle;
    return AURACORE_OK;
}

void auracore_backend_destroy(auracore_backend* backend)
{
    delete backend;
}

const char* auracore_last_error(auracore_backend* backend)
{
    if (backend == nullptr || !backend->backend) {
        return "";
    }
    backend->errorBuffer = backend->backend->lastError().toUtf8();
    return backend->errorBuffer.constData();
}

static auracore_status writeJson(const QByteArray& json, char** out_json)
{
    if (json.isEmpty()) {
        return AURACORE_ERROR_BACKEND;
    }
    *out_json = strdup(json.constData());
    return *out_json != nullptr ? AURACORE_OK : AURACORE_ERROR_OUT_OF_MEMORY;
}

auracore_status auracore_list_instances(auracore_backend* backend, char** out_json)
{
    if (backend == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->listInstances(), out_json);
}

auracore_status auracore_get_instance(auracore_backend* backend, const char* instance_id, char** out_json)
{
    if (backend == nullptr || instance_id == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->getInstance(QString::fromUtf8(instance_id)), out_json);
}

auracore_status auracore_detect_java(auracore_backend* backend, char** out_json)
{
    if (backend == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->detectJava(), out_json);
}

auracore_status auracore_list_component_lists(auracore_backend* backend, char** out_json)
{
    if (backend == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->listComponentLists(), out_json);
}

auracore_status auracore_list_component_versions(auracore_backend* backend, const char* uid, char** out_json)
{
    if (backend == nullptr || uid == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->listComponentVersions(QString::fromUtf8(uid)), out_json);
}

auracore_status auracore_probe_java(auracore_backend* backend, char** out_json)
{
    if (backend == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->probeJava(), out_json);
}
auracore_status auracore_refresh_metadata(auracore_backend* backend, char** out_json)
{
    if (backend == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->refreshMetadata(), out_json);
}

auracore_status auracore_refresh_component(auracore_backend* backend, const char* uid, char** out_json)
{
    if (backend == nullptr || uid == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->refreshComponent(QString::fromUtf8(uid)), out_json);
}
auracore_status auracore_create_instance(auracore_backend* backend,
                                         const char* name,
                                         const char* game_version,
                                         const char* group,
                                         char** out_json)
{
    if (backend == nullptr || name == nullptr || game_version == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    const QString groupName = group == nullptr ? QString() : QString::fromUtf8(group);
    return writeJson(backend->backend->createInstance(QString::fromUtf8(name), QString::fromUtf8(game_version), groupName), out_json);
}

auracore_status auracore_task_status(auracore_backend* backend, const char* task_id, char** out_json)
{
    if (backend == nullptr || task_id == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->taskStatus(QString::fromUtf8(task_id)), out_json);
}

auracore_status auracore_wait_task(auracore_backend* backend, const char* task_id, int timeout_ms, char** out_json)
{
    if (backend == nullptr || task_id == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->waitTask(QString::fromUtf8(task_id), timeout_ms), out_json);
}

auracore_status auracore_cancel_task(auracore_backend* backend, const char* task_id)
{
    if (backend == nullptr || task_id == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return backend->backend->cancelTask(QString::fromUtf8(task_id)) ? AURACORE_OK : AURACORE_ERROR_BACKEND;
}
auracore_status auracore_rename_instance(auracore_backend* backend, const char* id, const char* new_name, char** out_json)
{
    if (backend == nullptr || id == nullptr || new_name == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->renameInstance(QString::fromUtf8(id), QString::fromUtf8(new_name)), out_json);
}

auracore_status auracore_set_instance_group(auracore_backend* backend, const char* id, const char* group, char** out_json)
{
    if (backend == nullptr || id == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    const QString groupName = group == nullptr ? QString() : QString::fromUtf8(group);
    return writeJson(backend->backend->setInstanceGroup(QString::fromUtf8(id), groupName), out_json);
}

auracore_status auracore_set_instance_icon(auracore_backend* backend, const char* id, const char* icon_key, char** out_json)
{
    if (backend == nullptr || id == nullptr || icon_key == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->setInstanceIcon(QString::fromUtf8(id), QString::fromUtf8(icon_key)), out_json);
}

auracore_status auracore_delete_instance(auracore_backend* backend, const char* id, char** out_json)
{
    if (backend == nullptr || id == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->deleteInstance(QString::fromUtf8(id)), out_json);
}
void auracore_free(char* text)
{
    free(text);
}

}  // extern "C"