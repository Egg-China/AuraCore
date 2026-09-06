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
#include <QEventLoop>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>
#include <cstring>

#include "auracore/backend.h"

#include "CoreApplication.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "java/JavaChecker.h"
#include "java/JavaUtils.h"
#include "meta/Index.h"
#include "meta/Version.h"
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
QJsonObject instanceToJson(MinecraftInstance* instance)
{
    QJsonObject object;
    object.insert("id", instance->id());
    object.insert("name", instance->name());
    object.insert("dir", instance->instanceRoot());
    object.insert("icon", instance->iconKey());
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
        array.append(instanceToJson(instances->at(i)));
    }
    return toJson(QJsonDocument(array));
}

QByteArray Backend::getInstance(const QString& id)
{
    const auto* instances = m_core->instances();
    for (int i = 0; i < instances->count(); ++i) {
        auto* instance = instances->at(i);
        if (instance->id() == id) {
            return toJson(QJsonDocument(instanceToJson(instance)));
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

bool Backend::runTaskSync(const Task::Ptr& task)
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
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
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
void auracore_free(char* text)
{
    free(text);
}

}  // extern "C"