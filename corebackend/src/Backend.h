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

#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QString>

#include <functional>
#include <memory>
#include <vector>
#include "tasks/Task.h"

class CoreApplication;

namespace AuraCore {

/** Owns CoreApplication and serializes its read-only state to JSON. */
class Backend {
   public:
    ~Backend();

    static std::unique_ptr<Backend> create(const QString& dataPath, QString* error);

    QString lastError() const { return m_lastError; }

    QByteArray listInstances();
    QByteArray getInstance(const QString& id);
    QByteArray detectJava();
    QByteArray probeJava();
    QByteArray listComponentLists();
    QByteArray listComponentVersions(const QString& uid);
    QByteArray refreshMetadata();
    QByteArray refreshComponent(const QString& uid);

    QByteArray createInstance(const QString& name, const QString& gameVersion, const QString& group);
    QByteArray renameInstance(const QString& id, const QString& newName);
    QByteArray setInstanceGroup(const QString& id, const QString& group);
    QByteArray setInstanceIcon(const QString& id, const QString& iconKey);
    QByteArray deleteInstance(const QString& id);
    QByteArray exportInstance(const QString& id, const QString& outputPath);
    QByteArray importInstance(const QString& source, const QString& name, const QString& group);
    QByteArray listAccounts();
    QByteArray addOfflineAccount(const QString& username);
    QByteArray removeAccount(const QString& profileName);
    QByteArray setDefaultAccount(const QString& profileName);
    QByteArray beginMsaLogin();
    QByteArray msaLoginInfo(const QString& taskId);
    QByteArray launchInstance(const QString& id, const QString& accountProfile, const QString& offlineName);
    QByteArray stopInstance(const QString& id);
    QByteArray instanceLogs(const QString& id, int maxLines);
    QByteArray getSetting(const QString& key);
    QByteArray setSetting(const QString& key, const QJsonValue& value);
    QByteArray taskStatus(const QString& taskId);
    QByteArray waitTask(const QString& taskId, int timeoutMs);
    bool cancelTask(const QString& taskId);

   private:
    Backend(const QString& dataPath, bool ownApplication);

    bool loadMetaCache();

    struct TrackedTask {
        Task::Ptr task;
        QString type;
        bool finished = false;
        bool succeeded = false;
        bool aborted = false;
        QString error;
        QString status;
        qint64 progress = 0;
        qint64 progressTotal = 0;
        // Device-code login extras; filled once the provider answers.
        QString msaVerificationUrl;
        QString msaUserCode;
        int msaExpiresIn = 0;
        std::function<void()> onSuccess;
    };
    using TrackedTaskPtr = std::shared_ptr<TrackedTask>;

    TrackedTaskPtr trackTask(const Task::Ptr& task, const QString& type);
    void pruneFinishedTasks();
    static bool runTaskSync(const Task::Ptr& task, int valveMs = 30000);

    std::unique_ptr<CoreApplication> m_core;
    QString m_lastError;
    bool m_metaLoaded = false;

    // Owned core application created for non-Qt hosts such as JVM embedders.
    std::unique_ptr<QCoreApplication> m_ownedApplication;
    std::vector<std::unique_ptr<char[]>> m_applicationArguments;
    std::vector<char*> m_argumentPointers;
    int m_argumentCount = 1;

    QHash<QString, TrackedTaskPtr> m_tasks;
    int m_nextTaskId = 1;
};

}  // namespace AuraCore
