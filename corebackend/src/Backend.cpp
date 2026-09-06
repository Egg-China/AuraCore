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
#include <QUrl>
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
#include "minecraft/auth/AccountList.h"
#include "launch/LaunchTask.h"
#include "launch/LogModel.h"
#include "LaunchController.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "InstanceDirUpdate.h"
#include "InstanceImportTask.h"
#include "MMCZip.h"
#include "archive/ExportToZipTask.h"
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
        if (tracked->onSuccess) {
            tracked->onSuccess();
        }
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
QByteArray Backend::exportInstance(const QString& id, const QString& outputPath)
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
    if (outputPath.isEmpty()) {
        m_lastError = QStringLiteral("Export output path must not be empty");
        return {};
    }

    QFileInfoList files;
    if (!MMCZip::collectFileListRecursively(instance->instanceRoot(), nullptr, &files, nullptr)) {
        m_lastError = QStringLiteral("Could not collect instance files for export");
        return {};
    }

    // MultiMC-format zip: the archive root mirrors the instance directory, so
    // InstanceImportTask detects instance.cfg and imports it unchanged.
    const auto task = makeShared<MMCZip::ExportToZipTask>(outputPath, instance->instanceRoot(), files);
    const auto tracked = trackTask(task, QStringLiteral("export-instance"));
    QMetaObject::invokeMethod(task.get(), &Task::start, Qt::QueuedConnection);

    const QString taskId = QString::number(m_nextTaskId - 1);
    return toJson(QJsonDocument(QJsonObject{
        { "exporting", true },
        { "taskId", taskId },
        { "id", id },
        { "output", outputPath },
        { "files", double(files.size()) },
    }));
}

QByteArray Backend::importInstance(const QString& source, const QString& name, const QString& group)
{
    const QUrl url = QFile::exists(source) ? QUrl::fromLocalFile(QFileInfo(source).absoluteFilePath())
                                           : QUrl::fromUserInput(source);
    if (!url.isValid()) {
        m_lastError = QStringLiteral("Invalid import source: %1").arg(source);
        return {};
    }
    if (name.isEmpty()) {
        m_lastError = QStringLiteral("Import name must not be empty");
        return {};
    }

    // Local files and explicit http(s) sources are trusted; the launcher UI
    // layer can wrap untrusted downloads with its own vetting.
    auto* import = new InstanceImportTask(url, true, nullptr);
    import->setName(name);
    import->setIcon(QStringLiteral("default"));
    if (!group.isEmpty()) {
        import->setGroup(group);
    }

    Task* staging = m_core->instances()->wrapInstanceTask(import);
    if (staging == nullptr) {
        delete import;
        m_lastError = QStringLiteral("Could not stage the imported instance");
        return {};
    }

    const auto tracked = trackTask(Task::Ptr(staging), QStringLiteral("import-instance"));
    QMetaObject::invokeMethod(staging, &Task::start, Qt::QueuedConnection);

    const QString taskId = QString::number(m_nextTaskId - 1);
    return toJson(QJsonDocument(QJsonObject{
        { "importing", true },
        { "taskId", taskId },
        { "name", name },
        { "source", url.toString() },
    }));
}
QByteArray Backend::listAccounts()
{
    QJsonArray array;
    const auto* accounts = m_core->accounts();
    for (int i = 0; i < accounts->count(); ++i) {
        const auto& account = accounts->at(i);
        QJsonObject object;
        object.insert("profileName", account->profileName());
        object.insert("type", account->typeString());
        object.insert("internalId", account->internalId());
        object.insert("hasProfile", account->hasProfile());
        array.append(object);
    }
    return toJson(QJsonDocument(array));
}

QByteArray Backend::addOfflineAccount(const QString& username)
{
    if (username.isEmpty()) {
        m_lastError = QStringLiteral("Offline account username must not be empty");
        return {};
    }
    auto* accounts = m_core->accounts();
    for (int i = 0; i < accounts->count(); ++i) {
        if (accounts->at(i)->profileName().compare(username, Qt::CaseInsensitive) == 0) {
            return toJson(QJsonDocument(QJsonObject{
                { "added", false },
                { "profileName", username },
                { "error", QStringLiteral("An account with this profile name already exists") },
            }));
        }
    }

    const auto account = MinecraftAccount::createOffline(username);
    accounts->addAccount(account);
    return toJson(QJsonDocument(QJsonObject{
        { "added", true },
        { "profileName", account->profileName() },
        { "internalId", account->internalId() },
        { "type", account->typeString() },
    }));
}

QByteArray Backend::removeAccount(const QString& profileName)
{
    auto* accounts = m_core->accounts();
    for (int i = 0; i < accounts->count(); ++i) {
        if (accounts->at(i)->profileName() == profileName) {
            accounts->removeAccount(accounts->index(i, 0));
            return toJson(QJsonDocument(QJsonObject{
                { "removed", true },
                { "profileName", profileName },
            }));
        }
    }
    m_lastError = QStringLiteral("Unknown account profile name: %1").arg(profileName);
    return {};
}

QByteArray Backend::setDefaultAccount(const QString& profileName)
{
    auto* accounts = m_core->accounts();
    for (int i = 0; i < accounts->count(); ++i) {
        if (accounts->at(i)->profileName() == profileName) {
            accounts->setDefaultAccount(accounts->at(i));
            return toJson(QJsonDocument(QJsonObject{
                { "ok", true },
                { "profileName", profileName },
            }));
        }
    }
    m_lastError = QStringLiteral("Unknown account profile name: %1").arg(profileName);
    return {};
}
QByteArray Backend::beginMsaLogin()
{
    const auto account = MinecraftAccount::createBlankMSA();
    const auto flow = account->login(true);
    if (flow == nullptr) {
        m_lastError = QStringLiteral("Could not start the Microsoft login flow");
        return {};
    }

    const auto tracked = trackTask(flow, QStringLiteral("msa-login"));
    QObject::connect(flow.get(), &AuthFlow::authorizeWithBrowserWithExtra, [tracked](const QUrl& url, const QString& code, int expiresIn) {
        tracked->msaVerificationUrl = url.toString();
        tracked->msaUserCode = code;
        tracked->msaExpiresIn = expiresIn;
    });
    tracked->onSuccess = [this, account] { m_core->accounts()->addAccount(account); };
    QMetaObject::invokeMethod(flow.get(), &Task::start, Qt::QueuedConnection);

    const QString taskId = QString::number(m_nextTaskId - 1);
    return toJson(QJsonDocument(QJsonObject{
        { "started", true },
        { "taskId", taskId },
    }));
}

QByteArray Backend::msaLoginInfo(const QString& taskId)
{
    const auto it = m_tasks.constFind(taskId);
    if (it == m_tasks.constEnd() || !it.value()) {
        m_lastError = QStringLiteral("Unknown task id: %1").arg(taskId);
        return {};
    }
    const auto& tracked = it.value();
    if (tracked->msaUserCode.isEmpty()) {
        return toJson(QJsonDocument(QJsonObject{
            { "id", taskId },
            { "codeIssued", false },
        }));
    }
    return toJson(QJsonDocument(QJsonObject{
        { "id", taskId },
        { "codeIssued", true },
        { "verificationUrl", tracked->msaVerificationUrl },
        { "userCode", tracked->msaUserCode },
        { "expiresIn", tracked->msaExpiresIn },
    }));
}
QByteArray Backend::launchInstance(const QString& id, const QString& accountProfile, const QString& offlineName)
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

    MinecraftAccountPtr account;
    if (!accountProfile.isEmpty()) {
        auto* accounts = m_core->accounts();
        for (int i = 0; i < accounts->count(); ++i) {
            if (accounts->at(i)->profileName() == accountProfile) {
                account = accounts->at(i);
                break;
            }
        }
        if (account == nullptr) {
            m_lastError = QStringLiteral("Unknown account profile name: %1").arg(accountProfile);
            return {};
        }
    }

    shared_qobject_ptr<LaunchController> controller;
    if (!m_core->launch(instance, LaunchMode::Normal, nullptr, account, offlineName, &controller)) {
        m_lastError = QStringLiteral("Instance cannot be launched in its current state");
        return {};
    }
    if (controller == nullptr) {
        m_lastError = QStringLiteral("Launch controller was not created");
        return {};
    }

    const auto tracked = trackTask(controller, QStringLiteral("launch-instance"));
    Q_UNUSED(tracked);
    return toJson(QJsonDocument(QJsonObject{
        { "launched", true },
        { "taskId", QString::number(m_nextTaskId - 1) },
        { "id", id },
        { "account", accountProfile },
        { "offlineName", offlineName },
    }));
}

QByteArray Backend::stopInstance(const QString& id)
{
    auto* instances = m_core->instances();
    for (int i = 0; i < instances->count(); ++i) {
        if (instances->at(i)->id() == id) {
            const bool stopped = m_core->kill(instances->at(i));
            return toJson(QJsonDocument(QJsonObject{
                { "stopped", stopped },
                { "id", id },
            }));
        }
    }
    m_lastError = QStringLiteral("Unknown instance id: %1").arg(id);
    return {};
}
QByteArray Backend::instanceLogs(const QString& id, int maxLines)
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

    auto* task = instance->getLaunchTask();
    QJsonArray logs;
    if (task == nullptr) {
        return toJson(QJsonDocument(QJsonObject{
            { "id", id },
            { "running", false },
            { "logs", logs },
        }));
    }

    auto* model = task->getLogModel().get();
    const int total = model->rowCount();
    const int limit = maxLines <= 0 ? 200 : std::min(maxLines, 2000);
    const int first = std::max(0, total - limit);
    for (int row = first; row < total; ++row) {
        const QModelIndex index = model->index(row, 0);
        const auto level = static_cast<MessageLevelValue>(index.data(LogModel::LevelRole).toInt());
        logs.append(QJsonObject{
            { "level", MessageLevel(level).toString() },
            { "line", index.data(Qt::DisplayRole).toString() },
        });
    }
    return toJson(QJsonDocument(QJsonObject{
        { "id", id },
        { "running", instance->isRunning() },
        { "total", double(total) },
        { "logs", logs },
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
auracore_status auracore_export_instance(auracore_backend* backend, const char* id, const char* output_path, char** out_json)
{
    if (backend == nullptr || id == nullptr || output_path == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->exportInstance(QString::fromUtf8(id), QString::fromUtf8(output_path)), out_json);
}

auracore_status auracore_import_instance(auracore_backend* backend, const char* source, const char* name, const char* group, char** out_json)
{
    if (backend == nullptr || source == nullptr || name == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    const QString groupName = group == nullptr ? QString() : QString::fromUtf8(group);
    return writeJson(backend->backend->importInstance(QString::fromUtf8(source), QString::fromUtf8(name), groupName), out_json);
}
auracore_status auracore_list_accounts(auracore_backend* backend, char** out_json)
{
    if (backend == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->listAccounts(), out_json);
}

auracore_status auracore_add_offline_account(auracore_backend* backend, const char* username, char** out_json)
{
    if (backend == nullptr || username == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->addOfflineAccount(QString::fromUtf8(username)), out_json);
}

auracore_status auracore_remove_account(auracore_backend* backend, const char* profile_name, char** out_json)
{
    if (backend == nullptr || profile_name == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->removeAccount(QString::fromUtf8(profile_name)), out_json);
}

auracore_status auracore_set_default_account(auracore_backend* backend, const char* profile_name, char** out_json)
{
    if (backend == nullptr || profile_name == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->setDefaultAccount(QString::fromUtf8(profile_name)), out_json);
}
auracore_status auracore_begin_msa_login(auracore_backend* backend, char** out_json)
{
    if (backend == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->beginMsaLogin(), out_json);
}

auracore_status auracore_msa_login_info(auracore_backend* backend, const char* task_id, char** out_json)
{
    if (backend == nullptr || task_id == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->msaLoginInfo(QString::fromUtf8(task_id)), out_json);
}
auracore_status auracore_launch_instance(auracore_backend* backend,
                                         const char* id,
                                         const char* account_profile,
                                         const char* offline_name,
                                         char** out_json)
{
    if (backend == nullptr || id == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    const auto profile = account_profile == nullptr ? QString() : QString::fromUtf8(account_profile);
    const auto offline = offline_name == nullptr ? QString() : QString::fromUtf8(offline_name);
    return writeJson(backend->backend->launchInstance(QString::fromUtf8(id), profile, offline), out_json);
}

auracore_status auracore_stop_instance(auracore_backend* backend, const char* id, char** out_json)
{
    if (backend == nullptr || id == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->stopInstance(QString::fromUtf8(id)), out_json);
}
auracore_status auracore_read_instance_logs(auracore_backend* backend, const char* id, int max_lines, char** out_json)
{
    if (backend == nullptr || id == nullptr || out_json == nullptr) {
        return AURACORE_ERROR_INVALID_ARGUMENT;
    }
    return writeJson(backend->backend->instanceLogs(QString::fromUtf8(id), max_lines), out_json);
}
void auracore_free(char* text)
{
    free(text);
}

}  // extern "C"