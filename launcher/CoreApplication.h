// SPDX-License-Identifier: GPL-3.0-only
/*
 *  AuraCore - Minecraft launcher core
 *  Copyright (C) 2026 Aura Contributors
 *
 *  This file derives from Prism Launcher and incorporates work covered by:
 *      Copyright (C) 2022-2026 Prism Launcher Contributors
 *      Copyright 2013-2021 MultiMC Contributors
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

#include <QHash>
#include <QIcon>
#include <QMutex>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <memory>

#include "ApplicationFwd.h"
#include "LaunchMode.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/launch/MinecraftTarget.h"

class AccountList;
class HttpMetaCache;
class IconList;
class InstanceList;
class JavaInstallList;
class QNetworkAccessManager;
class QNetworkProxy;
class SettingsObject;
class QWidget;
class BaseProfilerFactory;
class BaseInstance;
class LaunchController;
class MinecraftInstance;

namespace Meta {
class Index;
}

/**
 * Headless replacement for Prism's QApplication-derived Application shell.
 *
 * The upstream core reaches the process-wide service container through the
 * APPLICATION macro. AuraCore keeps those call sites intact and installs its
 * own container here, avoiding a dependency on the Qt Widgets shell while the
 * HMCL-to-Prism core migration is in progress.
 */
class CoreApplication : public QObject {
    Q_OBJECT

   public:
    enum Capability {
        SupportsNone = 0,
        SupportsMSA = 1 << 0,
        SupportsFlame = 1 << 1,
        SupportsGameMode = 1 << 2,
        SupportsMangoHud = 1 << 3,
    };
    Q_DECLARE_FLAGS(Capabilities, Capability)

    /// Returns the process-wide core container installed by the host.
    static CoreApplication* instance();

    explicit CoreApplication(QString dataPath, QObject* parent = nullptr);
    ~CoreApplication() override;

    bool initialize();

    QString dataPath() const { return m_dataPath; }
    bool isPortable() const { return m_portable; }
    QString desktopFileName() const;

    SettingsObject* settings() const { return m_settings.get(); }
    SettingsObject* playtimeSettings() const { return m_playtimeSettings.get(); }

    InstanceList* instances() const { return m_instances.get(); }
    AccountList* accounts() const { return m_accounts.get(); }
    JavaInstallList* javalist();
    IconList* icons() const { return m_icons.get(); }

    QNetworkAccessManager* network() const { return m_network.get(); }
    HttpMetaCache* metacache() const { return m_metacache.get(); }
    Meta::Index* metadataIndex();

    const Capabilities capabilities() const { return m_capabilities; }
    const QMap<QString, std::shared_ptr<BaseProfilerFactory>>& profilers() const { return m_profilers; }

    QString getJarPath(QString jarFile);
    QString javaPath();
    QString getMSAClientID();
    QString getFlameAPIKey();
    QString getModrinthAPIToken();
    QString getUserAgent();
    QIcon logo() const;

    void updateProxySettings(QString proxyTypeStr, QString addr, int port, QString user, QString password);
    void updateCapabilities();

    // Detected native library paths, kept public to match upstream access patterns.
    QString m_detectedGLFWPath;
    QString m_detectedOpenALPath;
    QString m_detectedSDLPath;

    void addQSavePath(QString path);
    void removeQSavePath(QString path);
    bool checkQSavePath(QString path);

    // Shell integration hooks. AuraCore is headless; the host launcher or test
    // harness connects these to its own frontend behavior.
   signals:
    /// Forwards an OAuth reply payload from the host process to the MSA flow.
    void oauthReplyRecieved(QVariantMap oauthReply);

    void showMainWindowRequested();
    void showInstanceWindowRequested(MinecraftInstance* instance, QString page = {});
    void showGlobalSettingsRequested(QWidget* parent, QString page = {});
    void quitRequested();
    void windowsCloseRequested();

   public slots:
    /// Starts an instance through a headless LaunchController owned by the core.
    bool launch(MinecraftInstance* instance,
                LaunchMode mode = LaunchMode::Normal,
                MinecraftTarget::Ptr targetToJoin = nullptr,
                MinecraftAccountPtr accountToUse = nullptr,
                const QString& offlineName = QString());

    /// Aborts the controller currently managing the given instance, if any.
    bool kill(BaseInstance* instance);

    void showMainWindow() { emit showMainWindowRequested(); }
    void showInstanceWindow(MinecraftInstance* instance, QString page = {}) { emit showInstanceWindowRequested(instance, page); }
    void ShowGlobalSettings(QWidget* parent, QString page = {}) { emit showGlobalSettingsRequested(parent, page); }
    void quit() { emit quitRequested(); }
    void closeAllWindows() { emit windowsCloseRequested(); }

   private slots:
    /// Drops the finished controller from the tracked launch set.
    void controllerFinished();

   private:
    void registerCoreSettings();

    QString m_dataPath;
    QString m_rootPath;
    bool m_portable = false;
    Capabilities m_capabilities = SupportsNone;

    std::unique_ptr<SettingsObject> m_settings;
    std::unique_ptr<SettingsObject> m_playtimeSettings;
    std::unique_ptr<InstanceList> m_instances;
    std::unique_ptr<IconList> m_icons;
    std::unique_ptr<AccountList> m_accounts;
    std::unique_ptr<HttpMetaCache> m_metacache;
    std::unique_ptr<Meta::Index> m_metadataIndex;
    std::unique_ptr<JavaInstallList> m_javalist;
    std::unique_ptr<QNetworkAccessManager> m_network;
    QMap<QString, std::shared_ptr<BaseProfilerFactory>> m_profilers;



    QMutex m_qsaveResourcesMutex;
    QHash<QString, int> m_qsaveResources;

    QMutex m_instanceExtrasMutex;
    QHash<QString, shared_qobject_ptr<LaunchController>> m_controllers;
};
