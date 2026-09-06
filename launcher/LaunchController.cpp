// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "LaunchController.h"
#include "CoreApplication.h"
#include "launch/LaunchTask.h"
#include "minecraft/auth/AccountData.h"
#include "minecraft/auth/AccountList.h"

#include "net/NetUtils.h"

#include <QEventLoop>
#include <QInputDialog>
#include <QList>
#include <QPushButton>
#include <utility>

#include "BuildConfig.h"
#include "JavaCommon.h"
#include "launch/steps/PrintServers.h"
#include "launch/steps/TextPrint.h"
#include "tasks/Task.h"

LaunchController::LaunchController() = default;

void LaunchController::executeTask()
{
    if (!m_instance) {
        emitFailed(tr("No instance specified!"));
        return;
    }

    if (!JavaCommon::checkJVMArgs(m_instance->settings()->get("JvmArgs").toString(), m_parentWidget)) {
        emitFailed(tr("Invalid Java arguments specified. Please fix this first."));
        return;
    }

    login();
}

void LaunchController::decideAccount()
{
    if (m_accountToUse) {
        return;
    }

    // Select the account to use. If the instance has a specific account set, that will be used. Otherwise, the default account will be used
    auto* accounts = APPLICATION->accounts();
    const auto instanceAccountId = m_instance->settings()->get("InstanceAccountId").toString();
    const auto instanceAccountIndex = accounts->findAccountByProfileId(instanceAccountId);
    if (instanceAccountIndex == -1 || instanceAccountId.isEmpty()) {
        m_accountToUse = accounts->defaultAccount();
    } else {
        m_accountToUse = accounts->at(instanceAccountIndex);
    }

    if (!accounts->anyAccountIsValid()) {
        // A headless launch cannot add an account interactively.
        qWarning() << "No valid account is available; requesting the accounts page from the host";
        emit APPLICATION->showGlobalSettingsRequested(m_parentWidget, "accounts");
        emitFailed(tr("No account is available for launch"));
        return;
    }

    if (!m_accountToUse && accounts->anyAccountIsValid()) {
        // No interactive selection exists: fall back to the first account.
        qWarning() << "No default account set; using the first available account";
        m_accountToUse = accounts->at(0);
    }
}

LaunchDecision LaunchController::decideLaunchMode()
{
    if (!m_accountToUse || m_wantedLaunchMode == LaunchMode::Demo) {
        m_actualLaunchMode = LaunchMode::Demo;
        return LaunchDecision::Continue;
    }

    const auto* accounts = APPLICATION->accounts();
    MinecraftAccountPtr accountToCheck = nullptr;

    if (m_accountToUse->accountType() == AccountType::Offline) {
        // An explicitly selected offline account is directly launchable; the
        // entitlement hunt below only applies to online accounts, and routing
        // offline launches through the demo branch would block headless hosts.
        m_actualLaunchMode = m_wantedLaunchMode;
        return LaunchDecision::Continue;
    }

    accountToCheck = m_accountToUse->ownsMinecraft() ? m_accountToUse : nullptr;
    if (accountToCheck == nullptr) {
        // The selected online account lacks entitlement; borrow the state of
        // any entitled account before falling back to the demo decision.
        for (int i = 0; i < accounts->count(); i++) {
            if (const auto account = accounts->at(i); account->ownsMinecraft()) {
                accountToCheck = account;
                break;
            }
        }
    }

    if (!accountToCheck) {
        m_actualLaunchMode = LaunchMode::Demo;
        return LaunchDecision::Continue;
    }

    auto state = accountToCheck->accountState();
    const bool needsRefresh =
        m_wantedLaunchMode == LaunchMode::Normal && (state == AccountState::Offline || accountToCheck->shouldRefresh());
    if (state == AccountState::Unchecked || state == AccountState::Errored || needsRefresh) {
        accountToCheck->refresh();
        state = AccountState::Working;
    }

    if (state == AccountState::Working) {
        // refresh is in progress; wait for it without a dialog.
        QEventLoop loop;
        const auto task = accountToCheck->currentTask();
        if (task) {
            connect(task.get(), &Task::succeeded, &loop, &QEventLoop::quit);
            connect(task.get(), &Task::failed, &loop, &QEventLoop::quit);
            connect(task.get(), &Task::aborted, &loop, &QEventLoop::quit);
            loop.exec();
        }

        if (task && task->getState() == State::AbortedByUser) {
            return LaunchDecision::Abort;
        }

        state = accountToCheck->accountState();
    }

    QString reauthReason;
    switch (state) {
        case AccountState::Errored:
            reauthReason = tr("An error occurred while refreshing '%1'").arg(accountToCheck->profileName());
            break;
        case AccountState::Expired:
            reauthReason = tr("'%1' has expired and needs to be reauthenticated").arg(accountToCheck->profileName());
            break;
        case AccountState::Disabled:
            reauthReason = tr("The launcher's client identification has changed");
            break;
        case AccountState::Gone:
            reauthReason = tr("'%1' no longer exists on the servers").arg(accountToCheck->profileName());
            break;
        default:
            m_actualLaunchMode =
                state == AccountState::Online && m_wantedLaunchMode == LaunchMode::Normal ? LaunchMode::Normal : LaunchMode::Offline;
            return LaunchDecision::Continue;  // All good to go
    }

    if (reauthenticateAccount(accountToCheck, reauthReason)) {
        return LaunchDecision::Undecided;
    }

    return LaunchDecision::Abort;
}

bool LaunchController::askPlayDemo() const
{
    // AuraCore never silently switches a full launch into the demo.
    qWarning() << "Demo confirmation is unavailable in AuraCore; cancelling launch";
    return false;
}

QString LaunchController::askOfflineName(const QString& playerName, bool* ok)
{
    // Reuse the remembered name, falling back to the account's profile name.
    const QString lastOfflinePlayerName = APPLICATION->settings()->get("LastOfflinePlayerName").toString();
    QString usedname = lastOfflinePlayerName.isEmpty() ? playerName : lastOfflinePlayerName;

    qWarning() << "Using offline player name" << usedname << "(interactive naming is unavailable in AuraCore)";
    APPLICATION->settings()->set("LastOfflinePlayerName", usedname);

    if (ok != nullptr) {
        *ok = true;
    }
    return usedname;
}

void LaunchController::login()
{
    decideAccount();

    LaunchDecision decision = decideLaunchMode();
    while (decision == LaunchDecision::Undecided) {
        decision = decideLaunchMode();
    }
    if (decision == LaunchDecision::Abort) {
        emitAborted();
        return;
    }

    if (m_actualLaunchMode == LaunchMode::Demo) {
        if (m_wantedLaunchMode == LaunchMode::Demo || askPlayDemo()) {
            bool ok = false;
            auto name = askOfflineName("Player", &ok);
            if (ok) {
                m_session = std::make_shared<AuthSession>();
                m_session->MakeDemo(name, MinecraftAccount::uuidFromUsername(name).toString(QUuid::Id128));
                launchInstance();
                return;
            }
        }

        emitFailed(tr("No account selected for launch"));
        return;
    }

    m_session = std::make_shared<AuthSession>();
    m_session->launchMode = m_actualLaunchMode;
    m_accountToUse->fillSession(m_session);

    if (m_accountToUse->accountType() != AccountType::Offline) {
        if (m_actualLaunchMode == LaunchMode::Normal && !m_accountToUse->hasProfile()) {
            // Profile creation needs an interactive Microsoft flow, so headless launches stop here.
            qWarning() << "Account has no Minecraft profile; cannot set one up headlessly";
            emitAborted();
            return;
        }

        if (m_actualLaunchMode == LaunchMode::Offline && m_accountToUse->accountType() != AccountType::Offline) {
            bool ok = false;
            QString name = m_offlineName;
            if (name.isEmpty()) {
                name = askOfflineName(m_session->player_name, &ok);
                if (!ok) {
                    emitAborted();
                    return;
                }
            }
            m_session->MakeOffline(name);
        }
    }

    launchInstance();
}

bool LaunchController::reauthenticateAccount(const MinecraftAccountPtr& account, const QString& reason)
{
    Q_UNUSED(account)

    // Interactive re-login is unavailable; callers treat this as a launch abort.
    qWarning() << "Account reauthentication is unavailable in AuraCore:" << reason;
    return false;
}

void LaunchController::launchInstance()
{
    Q_ASSERT(m_instance != nullptr);
    Q_ASSERT(m_session.get() != nullptr);

    if (!m_instance->reloadSettings()) {
        qWarning() << "Couldn't load the instance profile";
        emitFailed(tr("Couldn't load the instance profile."));
        return;
    }

    m_launcher = m_instance->createLaunchTask(m_session, m_targetToJoin);
    if (!m_launcher) {
        emitFailed(tr("Couldn't instantiate a launcher."));
        return;
    }

    const auto showConsole = m_instance->settings()->get("ShowConsole").toBool();
    if (showConsole) {
        APPLICATION->showInstanceWindow(m_instance);
    }
    connect(m_launcher, &LaunchTask::readyForLaunch, this, &LaunchController::readyForLaunch);
    connect(m_launcher, &LaunchTask::succeeded, this, &LaunchController::onSucceeded);
    connect(m_launcher, &LaunchTask::failed, this, &LaunchController::onFailed);
    connect(m_launcher, &LaunchTask::requestProgress, this, &LaunchController::onProgressRequested);

    // Prepend Online and Auth Status
    QString online_mode;
    if (m_actualLaunchMode == LaunchMode::Normal) {
        online_mode = "online";

        // Prepend Server Status
        const QStringList servers = { "login.microsoftonline.com", "session.minecraft.net", "textures.minecraft.net", "api.mojang.com" };

        m_launcher->prependStep(makeShared<PrintServers>(m_launcher, servers));
    } else {
        online_mode = m_actualLaunchMode == LaunchMode::Demo ? "demo" : "offline";
    }

    m_launcher->prependStep(makeShared<TextPrint>(m_launcher, "Launched instance in " + online_mode + " mode\n", MessageLevel::Launcher));

    // Prepend Version
    {
        auto versionString = QString("%1 version: %2 (%3)")
                                 .arg(BuildConfig.LAUNCHER_DISPLAYNAME, BuildConfig.printableVersionString(), BuildConfig.BUILD_PLATFORM);
        m_launcher->prependStep(makeShared<TextPrint>(m_launcher, versionString + "\n", MessageLevel::Launcher));
    }
    m_launcher->start();
}

void LaunchController::readyForLaunch()
{
    if (!m_profiler) {
        m_launcher->proceed();
        return;
    }

    QString error;
    if (!m_profiler->check(&error)) {
        qWarning() << "Profiler check for" << m_profiler->name() << "failed:" << error;
        m_launcher->abort();
        emitFailed("Profiler startup failed!");
        return;
    }
    BaseProfiler* profilerInstance = m_profiler->createProfiler(m_launcher->instance(), this);

    connect(profilerInstance, &BaseProfiler::readyToLaunch, this, [this](const QString& message) {
        qWarning() << "Profiler ready; continuing launch:" << message;
        m_launcher->proceed();
    });
    connect(profilerInstance, &BaseProfiler::abortLaunch, this, [this](const QString& message) {
        qWarning() << "Couldn't start the profiler:" << message;
        m_launcher->abort();
        emitFailed("Profiler startup failed!");
    });
    profilerInstance->beginProfiling(m_launcher);
}

void LaunchController::onSucceeded()
{
    emitSucceeded();
}

void LaunchController::onFailed(QString reason)
{
    if (m_instance->settings()->get("ShowConsoleOnError").toBool()) {
        APPLICATION->showInstanceWindow(m_instance, "console");
    }
    emitFailed(std::move(reason));
}

void LaunchController::onProgressRequested(Task* task) const
{
    qWarning() << "Running interactive subtask headlessly:" << task->getStatus();
    QEventLoop loop;
    connect(task, &Task::succeeded, &loop, &QEventLoop::quit);
    connect(task, &Task::failed, &loop, &QEventLoop::quit);
    connect(task, &Task::aborted, &loop, &QEventLoop::quit);
    m_launcher->proceed();
    // The wrapper step is already running here; starting it again trips the
    // task assertions. A step that completed synchronously inside proceed()
    // must not enter the wait loop below either.
    if (!task->isRunning() && !task->isFinished()) {
        task->start();
    }
    if (!task->isFinished()) {
        loop.exec();
    }
}

bool LaunchController::abort()
{
    if (!m_launcher) {
        return true;
    }
    if (!m_launcher->canAbort()) {
        return false;
    }
    qWarning() << "Aborting the running instance without interactive confirmation";
    return m_launcher->abort();
}
