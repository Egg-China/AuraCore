// SPDX-License-Identifier: GPL-3.0-only
/*
 *  AuraCore - Minecraft launcher core
 *  Copyright (C) 2026 Aura Contributors
 *
 *  Minimal distilled variant of Prism's MMCIcon, retaining only the
 *  surface consumed by core shortcut/export flows.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QIcon>
#include <QString>

enum IconType : unsigned { Builtin, Transient, FileBased };

class MMCIcon {
   public:
    QString key;
    QString name;
    bool m_isBuiltIn = false;

    bool present() const { return !m_icon.isNull() || !key.isEmpty(); }
    QIcon icon() const { return m_icon; }
    bool isBuiltIn() const { return m_isBuiltIn; }
    QString getFilePath() const { return m_filePath; }

    void replace(IconType type, QIcon icon, QString path = {})
    {
        m_icon = std::move(icon);
        m_filePath = std::move(path);
        m_isBuiltIn = type == Builtin;
    }

   private:
    QIcon m_icon;
    QString m_filePath;
};

