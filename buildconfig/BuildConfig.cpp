// SPDX-License-Identifier: GPL-3.0-only
/*
 *  AuraCore - Minecraft launcher core
 *  Copyright (C) 2026 Aura Contributors
 *
 *  This file derives from Prism Launcher and incorporates work covered by:
 *      Copyright (C) 2022-2026 Prism Launcher Contributors
 *      Copyright (C) 2013-2021 MultiMC Contributors
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

#include "BuildConfig.h"

#include <QSysInfo>

const Config BuildConfig;

Config::Config()
{
    LAUNCHER_NAME = "AuraCore";
    LAUNCHER_APP_BINARY_NAME = "auracore";
    LAUNCHER_DISPLAYNAME = "AuraCore";
    LAUNCHER_COPYRIGHT = "Copyright (C) 2026 Aura Contributors";
    LAUNCHER_DOMAIN = "auracore.aura.invalid";
    LAUNCHER_CONFIGFILE = "auracore.cfg";
    LAUNCHER_GIT = "https://github.com/Egg-China/AuraCore";
    LAUNCHER_APPID = "org.auracore.AuraCore";
    LAUNCHER_ENVNAME = "AURACORE";

    VERSION_MAJOR = AURACORE_VERSION_MAJOR;
    VERSION_MINOR = AURACORE_VERSION_MINOR;
    VERSION_PATCH = AURACORE_VERSION_PATCH;

    BUILD_PLATFORM = AURACORE_BUILD_PLATFORM;
    BUILD_DATE = AURACORE_BUILD_TIMESTAMP;
    COMPILER_NAME = AURACORE_COMPILER_NAME;
    COMPILER_VERSION = AURACORE_COMPILER_VERSION;

    USER_AGENT = QStringLiteral("AuraCore/%1 (GitHub: Egg-China/AuraCore)").arg(versionString());
    GIT_COMMIT = AURACORE_GIT_COMMIT;
    GIT_TAG = AURACORE_GIT_TAG;
    GIT_REFSPEC = AURACORE_GIT_REFSPEC;

    LOGIN_CALLBACK_URL = "https://prismlauncher.org/successful-login";
    MSA_CLIENT_ID = "c36a9fb6-4f2a-41ff-90bd-ae7cc92031eb";
    FLAME_API_KEY = "$2a$10$wuAJuNZuted3NORVmpgUC.m8sI.pv1tOPKZyBgLFGjxFp/br0lZCC";
    META_URL = "https://meta.prismlauncher.org/v1/";

    GLFW_LIBRARY_NAME = "libglfw.so.3";
    OPENAL_LIBRARY_NAME = "libopenal.so.1";
    SDL_LIBRARY_NAME = "libSDL2-2.0.so.0";

    LEGACY_FMLLIBS_BASE_URL = "https://files.prismlauncher.org/fmllibs/";
    LEGACY_FTB_CDN_BASE_URL = "https://dist.creeper.host/FTB2/";
    ATL_DOWNLOAD_SERVER_URL = "https://download.nodecdn.net/containers/atl/";
    FTB_API_BASE_URL = "https://api.feed-the-beast.com/v1/modpacks/public";
    TECHNIC_API_BASE_URL = "https://api.technicpack.net/";
    TECHNIC_API_BUILD = "auracore";
    MODRINTH_STAGING_URL = "https://staging-api.modrinth.com/v2";
    MODRINTH_PROD_URL = "https://api.modrinth.com/v2";
    MODRINTH_MRPACK_HOSTS = { "cdn.modrinth.com", "github.com", "raw.githubusercontent.com", "gitlab.com" };
    MODRINTH_DOWNLOAD_HOST = "cdn.modrinth.com";
    FLAME_BASE_URL = "https://api.curseforge.com/v1";
    FLAME_DOWNLOAD_HOST = "edge.forgecdn.net";

    if (GIT_REFSPEC.startsWith("refs/heads/")) {
        VERSION_CHANNEL = GIT_REFSPEC;
        VERSION_CHANNEL.remove("refs/heads/");
    } else if (!GIT_COMMIT.isEmpty()) {
        VERSION_CHANNEL = GIT_COMMIT.mid(0, 8);
    } else {
        VERSION_CHANNEL = "unknown";
    }
}

QString Config::versionString() const
{
    return QString("%1.%2.%3").arg(VERSION_MAJOR).arg(VERSION_MINOR).arg(VERSION_PATCH);
}

QString Config::printableVersionString() const
{
    QString vstr = versionString();
    if (!VERSION_CHANNEL.isEmpty() && VERSION_CHANNEL != "stable" && VERSION_CHANNEL != "release") {
        vstr += "-" + VERSION_CHANNEL;
    }
    if (!GIT_COMMIT.isEmpty()) {
        vstr += " (" + GIT_COMMIT + ")";
    }
    return vstr;
}

