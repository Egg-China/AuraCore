#include "InstanceTask.h"
#include <QApplication>
#include <QDir>

#include "CoreApplication.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/MinecraftLoadAndCheck.h"
#include "settings/SettingsObject.h"
#include "tasks/SequentialTask.h"
#include <QDebug>

#include <QPushButton>

InstanceNameChange askForChangingInstanceName(QWidget* parent, const QString& oldName, const QString& newName)
{
    Q_UNUSED(parent)
    // AuraCore is headless: accept the name change derived from pack metadata.
    qWarning() << "Auto-accepting instance name change" << oldName << "->" << newName;
    return InstanceNameChange::ShouldChange;
}

ShouldUpdate askIfShouldUpdate(QWidget* parent, const QString& originalVersionName)
{
    if (APPLICATION->settings()->get("SkipModpackUpdatePrompt").toBool()) {
        return ShouldUpdate::SkipUpdating;
    }

    Q_UNUSED(parent)
    qWarning() << "Similar modpack found for" << originalVersionName << "- creating a separate instance (headless default)";
        return ShouldUpdate::SkipUpdating;
}

QString InstanceTask::name() const
{
    if (!m_modifiedName.isEmpty()) {
        return modifiedName();
    }
    if (!m_originalVersion.isEmpty()) {
        return QString("%1 %2").arg(m_originalName, m_originalVersion);
    }

    return m_originalName;
}

QString InstanceTask::originalName() const
{
    return m_originalName;
}

QString InstanceTask::modifiedName() const
{
    if (!m_modifiedName.isEmpty()) {
        return m_modifiedName;
    }
    return m_originalName;
}

QString InstanceTask::version() const
{
    return m_originalVersion;
}

void InstanceTask::setOriginalName(const QString& name, const QString& version)
{
    m_originalName = name;
    m_originalVersion = version;
}
void InstanceTask::setOverride(bool override, const QString& instanceIdToOverride)
{
    m_overrideExisting = override;
    if (!instanceIdToOverride.isEmpty()) {
        m_originalInstanceId = instanceIdToOverride;
    }
}

ShouldDeleteSaves askIfShouldDeleteSaves(QWidget* parent)
{
    Q_UNUSED(parent)
    qWarning() << "Keeping existing saves: interactive deletion confirmation is unavailable in AuraCore";
    return ShouldDeleteSaves::No;
}

void InstanceTask::scheduleToDelete(QWidget* parent, const QDir& dir, const QString& path, bool checkDisabled)
{
    if (path.isEmpty()) {
        return;
    }
    if (path.startsWith("saves/")) {
        if (m_shouldDeleteSaves == ShouldDeleteSaves::NotAsked) {
            m_shouldDeleteSaves = askIfShouldDeleteSaves(parent);
        }
        if (m_shouldDeleteSaves == ShouldDeleteSaves::No) {
            return;
        }
    }
    qDebug() << "Scheduling" << path << "for removal";
    m_filesToRemove.append(dir.absoluteFilePath(path));
    if (checkDisabled) {
        if (path.endsWith(".disabled")) {  // remove it if it was enabled/disabled by user
            m_filesToRemove.append(dir.absoluteFilePath(path.chopped(9)));
        } else {
            m_filesToRemove.append(dir.absoluteFilePath(path + ".disabled"));
        }
    }
}

void InstanceTask::downloadFiles(MinecraftInstance* inst)
{
    if (!APPLICATION->settings()->get("DownloadGameFilesDuringInstanceCreation").toBool()) {
        emitSucceeded();
        return;
    }
    setAbortable(true);
    setAbortButtonText(tr("Skip"));
    qDebug() << "Downloading game files";

    auto updateTasks = inst->createUpdateTask();
    if (updateTasks.isEmpty()) {
        emitSucceeded();
        return;
    }
    auto task = makeShared<SequentialTask>();
    task->addTask(makeShared<MinecraftLoadAndCheck>(inst, Net::Mode::Online));
    for (const auto& t : updateTasks) {
        task->addTask(t);
    }
    connect(task.get(), &Task::finished, this, [this, task] {
        if (!isRunning()) {
            return;
        }
        if (!task->wasSuccessful()) {
            qWarning() << "Could not download game files:" << task->failReason();
        }
        emitSucceeded();
    });
    propagateFromOther(task.get());
    setDetails(tr("Downloading game files"));

    m_gameFilesTask = task;
    m_gameFilesTask->start();
}

bool InstanceTask::abort()
{
    if (!canAbort()) {
        return false;
    }

    if (m_gameFilesTask) {
        return m_gameFilesTask->abort();
    }

    return Task::abort();
}
