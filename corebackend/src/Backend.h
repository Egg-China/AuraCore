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
#include <QString>

#include <memory>
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

   private:
    explicit Backend(std::unique_ptr<CoreApplication> core);

    bool loadMetaCache();
    static bool runTaskSync(const Task::Ptr& task);

    std::unique_ptr<CoreApplication> m_core;
    QString m_lastError;
    bool m_metaLoaded = false;
};

}  // namespace AuraCore