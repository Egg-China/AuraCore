// SPDX-License-Identifier: GPL-3.0-only
/*
 *  AuraCore - Minecraft launcher core
 *  Copyright (C) 2026 Aura Contributors
 *
 *  Directory-backed icon registry replacing Prism's Qt theme-aware list.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QAbstractListModel>
#include <QIcon>
#include <QString>
#include <QStringList>

#include "icons/MMCIcon.h"

class IconList : public QAbstractListModel {
    Q_OBJECT
   public:
    explicit IconList(const QStringList& builtinPaths, const QString& path, QObject* parent = nullptr);
    ~IconList() override = default;

    enum Roles { AppListRole = Qt::UserRole + 1 };

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    const MMCIcon* icon(const QString& key) const;
    void saveIcon(const QString& key, const QString& path, const char* format) const;
    bool deleteIcon(const QString& key);
    bool iconFileExists(const QString& key) const;
    void installIcon(const QString& file, const QString& name);
    void installIcons(const QStringList& iconFiles);

    QString directory() const { return m_path; }
    QStringList iconFiles() const { return m_files; }

   public slots:
    void directoryChanged(const QString& path);

   private:
    void reindex();

    QString m_path;
    QStringList m_builtinPaths;
    QStringList m_files;
    mutable QHash<QString, MMCIcon> m_icons;
};

