// SPDX-License-Identifier: GPL-3.0-only
/*
 *  AuraCore - Minecraft launcher core
 *  Copyright (C) 2026 Aura Contributors
 *
 *  Data type shared by mod-platform installers for files that cannot be
 *  downloaded through third-party launcher APIs.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QDebug>
#include <QString>
#include <QList>

struct BlockedMod {
    QString name;
    QString websiteUrl;
    QString hash;
    bool matched;
    QString localPath;
    QString targetFolder;
    bool disabled = false;
    bool move = false;
};

inline QDebug operator<<(QDebug debug, const BlockedMod& m)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "BlockedMod{ name=" << m.name << ", websiteUrl=" << m.websiteUrl << ", hash=" << m.hash
                    << ", matched=" << m.matched << ", localPath=" << m.localPath << " }";
    return debug;
}

