// SPDX-License-Identifier: GPL-3.0-only
/*
 *  AuraCore - Minecraft launcher core
 *  Copyright (C) 2026 Aura Contributors
 *
 *  This file derives from Prism Launcher and incorporates work covered by:
 *      Copyright (C) 2022-2026 Prism Launcher Contributors
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

#include <memory>
#include <string_view>

class BaseInstance;
using InstancePtr = std::shared_ptr<BaseInstance>;

class CoreApplication;

// Upstream source spells the global as APPLICATION_DYN when a null-check is
// desired. AuraCore always owns a valid process-wide container.
#define APPLICATION_DYN CoreApplication::instance()
#define APPLICATION CoreApplication::instance()

namespace Aura {
CoreApplication* coreApplication();
void setCoreApplication(CoreApplication* instance);
}

