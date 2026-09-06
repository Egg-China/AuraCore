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

#pragma once

#include <QList>
#include <QString>

/**
 * Holds the build-time identity and endpoint information consumed by the core.
 *
 * The layout intentionally mirrors Prism's generated configuration so core
 * call sites remain source-compatible during the migration.
 */
class Config {
   public:
    Config();

    QString LAUNCHER_NAME;
    QString LAUNCHER_APP_BINARY_NAME;
    QString LAUNCHER_DISPLAYNAME;
    QString LAUNCHER_COPYRIGHT;
    QString LAUNCHER_DOMAIN;
    QString LAUNCHER_CONFIGFILE;
    QString LAUNCHER_GIT;
    QString LAUNCHER_APPID;
    QString LAUNCHER_ENVNAME;

    int VERSION_MAJOR;
    int VERSION_MINOR;
    int VERSION_PATCH;
    QString VERSION_CHANNEL;

    QString BUILD_PLATFORM;
    QString BUILD_DATE;
    QString COMPILER_NAME;
    QString COMPILER_VERSION;

    QString USER_AGENT;
    QString GIT_COMMIT;
    QString GIT_TAG;
    QString GIT_REFSPEC;

    QString LOGIN_CALLBACK_URL;
    QString MSA_CLIENT_ID;
    QString FLAME_API_KEY;
    QString META_URL;

    QString GLFW_LIBRARY_NAME;
    QString OPENAL_LIBRARY_NAME;
    QString SDL_LIBRARY_NAME;

    QString LEGACY_FMLLIBS_BASE_URL;
    QString LEGACY_FTB_CDN_BASE_URL;
    QString ATL_DOWNLOAD_SERVER_URL;
    QString FTB_API_BASE_URL;
    QString TECHNIC_API_BASE_URL;
    QString TECHNIC_API_BUILD;
    QString MODRINTH_STAGING_URL;
    QString MODRINTH_PROD_URL;
    QStringList MODRINTH_MRPACK_HOSTS;
    QString MODRINTH_DOWNLOAD_HOST;
    QString FLAME_BASE_URL;
    QString FLAME_DOWNLOAD_HOST;

    QString DEFAULT_RESOURCE_BASE = "https://resources.download.minecraft.net/";
    QString LIBRARY_BASE = "https://libraries.minecraft.net/";

    QString versionString() const;
    QString printableVersionString() const;
};

extern const Config BuildConfig;

