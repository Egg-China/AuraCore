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

#include "CoreApplication.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QNetworkAccessManager>
#include <QGuiApplication>
#include <QNetworkProxy>
#include <QStandardPaths>

#include <utility>

#include "minecraft/auth/AccountList.h"
#include "BuildConfig.h"
#include "FileSystem.h"
#include "net/HttpMetaCache.h"
#ifdef Q_OS_LINUX
#include <dlfcn.h>
#include "LibraryUtils.h"
#if __has_include(<gamemode_client.h>)
#include <gamemode_client.h>
#define AURACORE_HAVE_GAMEMODE
#endif
#endif
#include "icons/IconList.h"
#include "InstanceList.h"
#include "java/JavaInstallList.h"
#include "LaunchController.h"
#include "MTPixmapCache.h"
#include "meta/Index.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/INISettingsObject.h"
#include "SysInfo.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"

// Defined here (as in upstream Application.cpp) because MTPixmapCache.h only declares it.
PixmapCache* PixmapCache::s_instance = nullptr;

namespace {
CoreApplication* s_instance = nullptr;
}  // namespace

CoreApplication* CoreApplication::instance()
{
    return s_instance;
}

namespace Aura {
CoreApplication* coreApplication() { return CoreApplication::instance(); }

void setCoreApplication(CoreApplication* instance) { s_instance = instance; }
}  // namespace Aura

CoreApplication::CoreApplication(QString dataPath, QObject* parent) : QObject(parent), m_dataPath(std::move(dataPath))
{
    Q_ASSERT(s_instance == nullptr);
    s_instance = this;

    QDir::setCurrent(m_dataPath);
    m_rootPath = QCoreApplication::applicationDirPath();
    m_portable = QFile::exists("portable.txt");

    auto* pixmaps = new PixmapCache(this);
    PixmapCache::setInstance(pixmaps);
}

CoreApplication::~CoreApplication()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool CoreApplication::initialize()
{
    if (m_dataPath.isEmpty()) {
        m_dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (!FS::ensureFolderPathExists(m_dataPath)) {
        qCritical() << "Could not create AuraCore data path" << m_dataPath;
        return false;
    }
    QDir::setCurrent(m_dataPath);

    registerCoreSettings();

    m_network = std::make_unique<QNetworkAccessManager>();
    updateProxySettings(m_settings->get("ProxyType").toString(), m_settings->get("ProxyAddr").toString(),
                        m_settings->get("ProxyPort").value<qint16>(), m_settings->get("ProxyUser").toString(),
                        m_settings->get("ProxyPass").toString());

    // Instance icons remain useful for shortcut/export flows. AuraCore ships a
    // directory-backed list without Prism's compiled-in theme assets.
    {
        auto setting = m_settings->getSetting("IconsDir");
        m_icons.reset(new IconList({}, setting->get().toString()));
    }

    {
        QStringList allInstDirs;
        const QString instDir = m_settings->get("InstanceDir").toString();
        allInstDirs << instDir;
        for (const auto& dir : m_settings->get("AdditionalInstanceDirs").toStringList()) {
            if (!dir.isEmpty() && !allInstDirs.contains(dir)) {
                allInstDirs << dir;
            }
        }
        m_instances.reset(new InstanceList(m_settings.get(), allInstDirs, this));
        m_instances->loadList();
    }

    {
        m_accounts.reset(new AccountList(this));
        m_accounts->setListFilePath("accounts.json", true);
        m_accounts->loadList();
        m_accounts->fillQueue();
    }

    {
        m_metacache.reset(new HttpMetaCache("metacache"));
        m_metacache->addBase("asset_indexes", QDir("assets/indexes").absolutePath());
        m_metacache->addBase("libraries", QDir("libraries").absolutePath());
        m_metacache->addBase("fmllibs", QDir("mods/minecraftforge/libs").absolutePath());
        m_metacache->addBase("general", QDir("cache").absolutePath());
        m_metacache->addBase("ATLauncherPacks", QDir("cache/ATLauncherPacks").absolutePath());
        m_metacache->addBase("FTBPacks", QDir("cache/FTBPacks").absolutePath());
        m_metacache->addBase("TechnicPacks", QDir("cache/TechnicPacks").absolutePath());
        m_metacache->addBase("FlamePacks", QDir("cache/FlamePacks").absolutePath());
        m_metacache->addBase("FlameMods", QDir("cache/FlameMods").absolutePath());
        m_metacache->addBase("ModrinthPacks", QDir("cache/ModrinthPacks").absolutePath());
        m_metacache->addBase("ModrinthModpacks", QDir("cache/ModrinthModpacks").absolutePath());
        m_metacache->addBase("translations", QDir("translations").absolutePath());
        m_metacache->addBase("meta", QDir("meta").absolutePath());
        m_metacache->addBase("java", QDir("cache/java").absolutePath());
        m_metacache->addBase("feed", QDir("cache/feed").absolutePath());
        m_metacache->Load();
    }

    updateCapabilities();
    (void)metadataIndex();
    return true;
}

void CoreApplication::registerCoreSettings()
{
    m_settings = std::make_unique<INISettingsObject>(BuildConfig.LAUNCHER_CONFIGFILE, this);

    // Concurrency and networking
    m_settings->registerSetting("NumberOfConcurrentTasks", 10);
    m_settings->registerSetting("NumberOfConcurrentDownloads", 6);
    m_settings->registerSetting("NumberOfManualRetries", 1);
    m_settings->registerSetting("RequestTimeout", 60);
    m_settings->registerSetting("UserAgentOverride", QString());
    m_settings->registerSetting("MetaRefreshOnLaunch", true);
    m_settings->registerSetting("CloseAfterLaunch", false);
    m_settings->registerSetting("QuitAfterGameStop", false);
    m_settings->registerSetting("MetaURLOverride", QString());
    m_settings->registerSetting("MSAClientIDOverride", QString());
    m_settings->registerSetting("FlameKeyOverride", QString());
    m_settings->registerSetting("ModrinthToken", QString());

    // Proxy
    m_settings->registerSetting("ProxyType", "None");
    m_settings->registerSetting({ "ProxyAddr", "ProxyHostName" }, "127.0.0.1");
    m_settings->registerSetting("ProxyPort", 8080);
    m_settings->registerSetting({ "ProxyUser", "ProxyUsername" }, QString());
    m_settings->registerSetting({ "ProxyPass", "ProxyPassword" }, QString());

    // Folders
    m_settings->registerSetting("InstanceDir", "instances");
    m_settings->registerSetting("AdditionalInstanceDirs", QVariant(QStringList()));
    m_settings->registerSetting("LastUsedGroupForNewInstance", QString());
    m_settings->registerSetting("LastUsedInstDirForNewInstance", "");
    m_settings->registerSetting({ "CentralModsDir", "ModsDir" }, "mods");
    m_settings->registerSetting("IconsDir", "icons");
    m_settings->registerSetting("DownloadsDir", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    m_settings->registerSetting("DownloadsDirWatchRecursive", false);
    m_settings->registerSetting("MoveModsFromDownloadsDir", false);
    m_settings->registerSetting("SkinsDir", "skins");
    m_settings->registerSetting("JavaDir", "java");

    // Console behaviour (BaseInstance overrides these)
    m_settings->registerSetting("ConsoleMaxLines", 100000);
    m_settings->registerSetting("ConsoleOverflowStop", true);
    m_settings->registerSetting("ShowConsole", false);
    m_settings->registerSetting("AutoCloseConsole", false);
    m_settings->registerSetting("ShowConsoleOnError", true);
    m_settings->registerSetting("LogPrePostOutput", true);

    // Window size defaults
    m_settings->registerSetting({ "LaunchMaximized", "MCWindowMaximize" }, false);
    m_settings->registerSetting({ "MinecraftWinWidth", "MCWindowWidth" }, 854);
    m_settings->registerSetting({ "MinecraftWinHeight", "MCWindowHeight" }, 480);

    // Memory
    m_settings->registerSetting({ "MinMemAlloc", "MinMemoryAlloc" }, 512);
    m_settings->registerSetting({ "MaxMemAlloc", "MaxMemoryAlloc" }, SysInfo::defaultMaxJvmMem());
    m_settings->registerSetting("PermGen", 128);
    m_settings->registerSetting("LowMemWarning", true);

    // Java settings
    m_settings->registerSetting("JavaPath", "");
    m_settings->registerSetting("JavaSignature", "");
    m_settings->registerSetting("JavaArchitecture", "");
    m_settings->registerSetting("JavaRealArchitecture", "");
    m_settings->registerSetting("JavaVersion", "");
    m_settings->registerSetting("JavaVendor", "");
    m_settings->registerSetting("LastHostname", "");
    m_settings->registerSetting("JvmArgs", "");
    m_settings->registerSetting("IgnoreJavaCompatibility", false);
    m_settings->registerSetting("IgnoreJavaWizard", false);
    const auto defaultEnableAutoJava = m_settings->get("JavaPath").toString().isEmpty();
    m_settings->registerSetting("AutomaticJavaSwitch", defaultEnableAutoJava);
    m_settings->registerSetting("AutomaticJavaDownload", defaultEnableAutoJava);
    m_settings->registerSetting("UserAskedAboutAutomaticJavaDownload", false);

    // Legacy settings
    m_settings->registerSetting("OnlineFixes", false);

    // Native library workarounds
    m_settings->registerSetting("UseNativeOpenAL", false);
    m_settings->registerSetting("CustomOpenALPath", "");
    m_settings->registerSetting("UseNativeGLFW", false);
    m_settings->registerSetting("CustomGLFWPath", "");
    m_settings->registerSetting("UseNativeSDL", false);
    m_settings->registerSetting("CustomSDLPath", "");

    // Performance related options
    m_settings->registerSetting("EnableFeralGamemode", false);
    m_settings->registerSetting("EnableMangoHud", false);
    m_settings->registerSetting("UseDiscreteGpu", false);
    m_settings->registerSetting("UseZink", false);

    // Game time
    m_settings->registerSetting("ShowGameTime", true);
    m_settings->registerSetting("ShowGlobalGameTime", true);
    m_settings->registerSetting("RecordGameTime", true);
    m_settings->registerSetting("ShowGameTimeWithoutDays", false);

    // Minecraft mods
    m_settings->registerSetting("ModMetadataDisabled", false);
    m_settings->registerSetting("ModDependenciesDisabled", false);
    m_settings->registerSetting("SkipModpackUpdatePrompt", false);
    m_settings->registerSetting("ShowModIncompat", false);
    m_settings->registerSetting("DownloadGameFilesDuringInstanceCreation", false);

    // Minecraft offline player name
    m_settings->registerSetting("LastOfflinePlayerName", QString());

    // Wrapper command for launch
    m_settings->registerSetting("WrapperCommand", "");
    m_settings->registerSetting("Env", "{}");

    // Custom commands (BaseInstance overrides these)
    m_settings->registerSetting({ "PreLaunchCommand", "PreLaunchCmd" }, "");
    m_settings->registerSetting({ "PostExitCommand", "PostExitCmd" }, "");

    // Playtime is tracked in a machine-local file, independently of
    // machine-specific configuration.
    m_playtimeSettings = std::make_unique<INISettingsObject>(QString("playtime.cfg"), this);
    m_playtimeSettings->registerSetting("TotalPlayTime", 0);
    m_playtimeSettings->registerSetting("TotalPlayTimeMigrated", false);
}

QString CoreApplication::desktopFileName() const
{
    return QGuiApplication::desktopFileName();
}

JavaInstallList* CoreApplication::javalist()
{
    if (!m_javalist) {
        m_javalist.reset(new JavaInstallList());
    }
    return m_javalist.get();
}

Meta::Index* CoreApplication::metadataIndex()
{
    if (!m_metadataIndex) {
        m_metadataIndex.reset(new Meta::Index());
    }
    return m_metadataIndex.get();
}

void CoreApplication::addQSavePath(QString path)
{
    QMutexLocker locker(&m_qsaveResourcesMutex);
    m_qsaveResources[path] = m_qsaveResources.value(path, 0) + 1;
}

void CoreApplication::removeQSavePath(QString path)
{
    QMutexLocker locker(&m_qsaveResourcesMutex);
    auto count = m_qsaveResources.value(path, 0);
    if (count > 0) {
        count--;
    }
    if (count == 0) {
        m_qsaveResources.remove(path);
    } else {
        m_qsaveResources[path] = count;
    }
}

bool CoreApplication::checkQSavePath(QString path)
{
    QMutexLocker locker(&m_qsaveResourcesMutex);
    return m_qsaveResources.contains(path);
}

void CoreApplication::updateProxySettings(QString proxyTypeStr, QString addr, int port, QString user, QString password)
{
    if (proxyTypeStr == "SOCKS5") {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, addr, port, user, password));
    } else if (proxyTypeStr == "HTTP") {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, addr, port, user, password));
    } else {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }
}

bool CoreApplication::launch(MinecraftInstance* instance,
                             LaunchMode mode,
                             MinecraftTarget::Ptr targetToJoin,
                             MinecraftAccountPtr accountToUse,
                             const QString& offlineName,
                             shared_qobject_ptr<LaunchController>* outController)
{
    if (!instance->canLaunch()) {
        if (instance->isRunning()) {
            showInstanceWindow(instance, "console");
            return true;
        }
        qWarning() << "Cannot launch instance" << instance->id() << "in its current state.";
        return false;
    }

    shared_qobject_ptr<LaunchController> controller;
    {
        QMutexLocker locker(&m_instanceExtrasMutex);
        controller.reset(new LaunchController());
        controller->setInstance(instance);
        controller->setLaunchMode(mode);
        controller->setProfiler(m_profilers.value(instance->settings()->get("Profiler").toString()).get());
        controller->setTargetToJoin(targetToJoin);
        controller->setAccountToUse(accountToUse);
        controller->setOfflineName(offlineName);
        connect(controller.get(), &LaunchController::finished, this, &CoreApplication::controllerFinished);
        m_controllers.insert(instance->id(), controller);
        if (outController != nullptr) {
            *outController = controller;
        }
    }
    QMetaObject::invokeMethod(controller.get(), &Task::start, Qt::QueuedConnection);
    return true;
}

bool CoreApplication::kill(BaseInstance* instance)
{
    if (!instance->isRunning()) {
        qWarning() << "Attempted to kill instance" << instance->id() << ", which isn't running.";
        return false;
    }

    shared_qobject_ptr<LaunchController> controller;
    {
        QMutexLocker locker(&m_instanceExtrasMutex);
        controller = m_controllers.value(instance->id());
    }
    if (controller) {
        return controller->abort();
    }
    return true;
}

void CoreApplication::controllerFinished()
{
    auto* controller = qobject_cast<LaunchController*>(sender());
    if (!controller) {
        return;
    }

    QMutexLocker locker(&m_instanceExtrasMutex);
    m_controllers.remove(controller->id());
}

QIcon CoreApplication::logo() const
{
    // AuraCore ships no bundled theme assets; hosts provide their own icon.
    return {};
}

void CoreApplication::updateCapabilities()
{
    m_capabilities = SupportsNone;
    if (!getMSAClientID().isEmpty()) {
        m_capabilities |= SupportsMSA;
    }
    if (!getFlameAPIKey().isEmpty()) {
        m_capabilities |= SupportsFlame;
    }
#ifdef AURACORE_HAVE_GAMEMODE
    if (gamemode_query_status() >= 0) {
        m_capabilities |= SupportsGameMode;
    }
#endif
#ifdef Q_OS_LINUX
    if (!LibraryUtils::findMangoHud().isEmpty()) {
        m_capabilities |= SupportsMangoHud;
    }
#endif
}

QString CoreApplication::getJarPath(QString jarFile)
{
    QStringList potentialPaths = { FS::PathCombine(m_rootPath, "jars"), FS::PathCombine(QCoreApplication::applicationDirPath(), "jars"),
                                   FS::PathCombine(QCoreApplication::applicationDirPath(), "..", "jars") };
    // Hosts and test harnesses may relocate the jar bundle.
    const QString overrideDir = qEnvironmentVariable("AURACORE_JARS_DIR");
    if (!overrideDir.isEmpty()) {
        potentialPaths.prepend(overrideDir);
    }
    for (const QString& p : potentialPaths) {
        QString jarPath = FS::PathCombine(p, jarFile);
        if (QFileInfo(jarPath).isFile()) {
            return jarPath;
        }
    }
    return {};
}

QString CoreApplication::javaPath()
{
    return m_settings->get("JavaDir").toString();
}

QString CoreApplication::getMSAClientID()
{
    QString override = m_settings->get("MSAClientIDOverride").toString();
    if (!override.isEmpty()) {
        return override;
    }
    return BuildConfig.MSA_CLIENT_ID;
}

QString CoreApplication::getFlameAPIKey()
{
    QString override = m_settings->get("FlameKeyOverride").toString();
    if (!override.isEmpty()) {
        return override;
    }
    return BuildConfig.FLAME_API_KEY;
}

QString CoreApplication::getModrinthAPIToken()
{
    QString override = m_settings->get("ModrinthToken").toString();
    if (!override.isEmpty()) {
        return override;
    }
    return QString();
}

QString CoreApplication::getUserAgent()
{
    QString override = m_settings->get("UserAgentOverride").toString();
    if (!override.isEmpty()) {
        return override.replace("$LAUNCHER_VER", BuildConfig.printableVersionString());
    }
    return BuildConfig.USER_AGENT;
}

