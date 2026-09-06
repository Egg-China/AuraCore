// SPDX-License-Identifier: GPL-3.0-only
/*
 *  AuraCore - Minecraft launcher core
 *  Copyright (C) 2026 Aura Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "icons/IconList.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QtAlgorithms>

#include "FileSystem.h"

IconList::IconList(const QStringList& builtinPaths, const QString& path, QObject* parent)
    : QAbstractListModel(parent), m_path(path), m_builtinPaths(builtinPaths)
{
    FS::ensureFolderPathExists(m_path);
    reindex();
}

void IconList::reindex()
{
    beginResetModel();
    m_files.clear();
    QDir iconDir(m_path);
    const QFileInfoList entries = iconDir.entryInfoList(QDir::Files, QDir::Name);
    for (const auto& entry : entries) {
        m_files.append(entry.absoluteFilePath());
    }
    endResetModel();
}

void IconList::directoryChanged(const QString& path)
{
    m_path = path;
    FS::ensureFolderPathExists(m_path);
    reindex();
}

int IconList::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_files.size();
}

QVariant IconList::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_files.size()) {
        return {};
    }
    const QString file = m_files.at(index.row());
    if (role == Qt::DisplayRole || role == AppListRole) {
        return QFileInfo(file).completeBaseName();
    }
    return {};
}

static QString keyFromFile(const QString& file)
{
    return QFileInfo(file).completeBaseName();
}

const MMCIcon* IconList::icon(const QString& key) const
{
    for (const QString& file : m_files) {
        if (keyFromFile(file) == key) {
            auto& entry = m_icons[key];
            entry.key = key;
            entry.name = key;
            entry.replace(FileBased, QIcon(file), file);
            return &entry;
        }
    }
    for (const QString& dirPath : m_builtinPaths) {
        QDir dir(dirPath);
        const QFileInfoList entries = dir.entryInfoList({ key + ".*" }, QDir::Files);
        if (!entries.isEmpty()) {
            auto& entry = m_icons[key];
            entry.key = key;
            entry.name = key;
            entry.replace(Builtin, QIcon(entries.first().absoluteFilePath()), entries.first().absoluteFilePath());
            return &entry;
        }
    }
    return nullptr;
}

void IconList::saveIcon(const QString& key, const QString& path, const char* format) const
{
    const MMCIcon* entry = icon(key);
    if (entry == nullptr) {
        return;
    }
    entry->icon().pixmap(1024, 1024).save(path, format);
}

bool IconList::deleteIcon(const QString& key)
{
    bool removed = false;
    QDir dir(m_path);
    for (const QString& file : m_files) {
        if (keyFromFile(file) == key) {
            removed |= dir.remove(QFileInfo(file).fileName());
        }
    }
    if (removed) {
        reindex();
    }
    return removed;
}

bool IconList::iconFileExists(const QString& key) const
{
    for (const QString& file : m_files) {
        if (keyFromFile(file) == key) {
            return true;
        }
    }
    return false;
}

void IconList::installIcon(const QString& file, const QString& name)
{
    QString target = FS::PathCombine(m_path, name);
    if (QFile::exists(target)) {
        QFile::remove(target);
    }
    QFile::copy(file, target);
    reindex();
}

void IconList::installIcons(const QStringList& iconFiles)
{
    for (const QString& file : iconFiles) {
        installIcon(file, QFileInfo(file).fileName());
    }
}

