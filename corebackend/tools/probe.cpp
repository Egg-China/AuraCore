// SPDX-License-Identifier: GPL-3.0-only
/*
 *  AuraCore - read-only backend probe
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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <cstring>
#include <string>

#include "auracore/backend.h"

namespace {

void printQuery(const char* label, auracore_backend* backend, auracore_status (*query)(auracore_backend*, char**))
{
    char* json = nullptr;
    const auracore_status status = query(backend, &json);
    std::printf("--- %s (status %d) ---\n", label, int(status));
    if (json != nullptr) {
        std::puts(json);
        auracore_free(json);
    } else {
        std::puts(auracore_last_error(backend));
    }
}

}  // namespace

// Tiny scanner for flat JSON string values such as "taskId":"7". Returns
// false when the key is absent; good enough for the probe's own responses.
static bool findJsonValue(const char* json, const char* key, char* out, size_t outSize)
{
    const std::string needle = std::string("\"") + key + "\":\"";
    const char* start = std::strstr(json, needle.c_str());
    if (start == nullptr) {
        return false;
    }
    start += needle.size();
    const char* end = std::strchr(start, '"');
    if (end == nullptr || size_t(end - start) >= outSize) {
        return false;
    }
    std::memcpy(out, start, size_t(end - start));
    out[end - start] = '\0';
    return true;
}
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    QString dataPath;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--data") == 0) {
            dataPath = QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    if (dataPath.isEmpty()) {
        dataPath = QDir::current().absoluteFilePath("auracore-probe-data");
    }
    QDir().mkpath(dataPath);

    std::printf("AuraCore ABI %d\n", auracore_abi_version());

    auracore_backend* backend = nullptr;
    const QByteArray dataPathUtf8 = dataPath.toUtf8();
    auracore_status status = auracore_backend_create(dataPathUtf8.constData(), &backend);
    if (status != AURACORE_OK || backend == nullptr) {
        std::fprintf(stderr, "backend create failed: %d\n", int(status));
        return 1;
    }

    printQuery("instances", backend, auracore_list_instances);
    printQuery("java", backend, auracore_detect_java);
    printQuery("probe-java", backend, auracore_probe_java);
    printQuery("component-lists", backend, auracore_list_component_lists);
    printQuery("refresh-metadata", backend, auracore_refresh_metadata);

    {
        char* refreshJson = nullptr;
        const auracore_status refreshStatus = auracore_refresh_component(backend, "net.minecraft", &refreshJson);
        std::printf("--- refresh-component net.minecraft (status %d) ---\n", int(refreshStatus));
        if (refreshJson != nullptr) {
            std::puts(refreshJson);
            auracore_free(refreshJson);
        }
    }

    char* versionJson = nullptr;
    const auracore_status versionStatus = auracore_list_component_versions(backend, "net.minecraft", &versionJson);
    std::printf("--- component-versions net.minecraft (status %d) ---\n", int(versionStatus));
    if (versionJson != nullptr) {
        std::puts(versionJson);
        auracore_free(versionJson);
    }

    {
        char* createJson = nullptr;
        const auracore_status createStatus = auracore_create_instance(backend, "Aura Probe", "1.20.1", nullptr, &createJson);
        std::printf("--- create-instance (status %d) ---\n", int(createStatus));
        if (createJson != nullptr) {
            std::puts(createJson);
            char* taskId = strdup(createJson);
            auracore_free(createJson);

            // extract taskId with a tiny scan: {"created":true,"taskId":"N",...
            char* idStart = std::strstr(taskId, "\"taskId\":\"");
            if (idStart != nullptr) {
                idStart += std::strlen("\"taskId\":\"");
                char* idEnd = std::strchr(idStart, '"');
                if (idEnd != nullptr) {
                    *idEnd = '\0';
                    char* waitJson = nullptr;
                    const auracore_status waitStatus = auracore_wait_task(backend, idStart, 180000, &waitJson);
                    std::printf("--- wait-task %s (status %d) ---\n", idStart, int(waitStatus));
                    if (waitJson != nullptr) {
                        std::puts(waitJson);
                        auracore_free(waitJson);
                    }
                }
            }
            free(taskId);
        }
        printQuery("instances-after-create", backend, auracore_list_instances);
    }
    {
        char currentId[128] = "Aura Probe";
        char* renameJson = nullptr;
        const auracore_status renameStatus = auracore_rename_instance(backend, currentId, "Aura Probe 2", &renameJson);
        std::printf("--- rename-instance (status %d) ---\n", int(renameStatus));
        if (renameJson != nullptr) {
            std::puts(renameJson);
            char newId[128];
            if (findJsonValue(renameJson, "id", newId, sizeof(newId))) {
                std::snprintf(currentId, sizeof(currentId), "%s", newId);
            }
            auracore_free(renameJson);
        }

        char* groupJson = nullptr;
        const auracore_status groupStatus = auracore_set_instance_group(backend, currentId, "E2E Group", &groupJson);
        std::printf("--- set-group (status %d) ---\n", int(groupStatus));
        if (groupJson != nullptr) {
            std::puts(groupJson);
            auracore_free(groupJson);
        }

        char* iconJson = nullptr;
        const auracore_status iconStatus = auracore_set_instance_icon(backend, currentId, "vanilla", &iconJson);
        std::printf("--- set-icon (status %d) ---\n", int(iconStatus));
        if (iconJson != nullptr) {
            std::puts(iconJson);
            auracore_free(iconJson);
        }

        printQuery("instances-after-edit", backend, auracore_list_instances);

        const QString exportPath = QDir(dataPath).absoluteFilePath("export-test.zip");
        char* exportJson = nullptr;
        const QByteArray exportPathUtf8 = exportPath.toUtf8();
        const auracore_status exportStatus =
            auracore_export_instance(backend, currentId, exportPathUtf8.constData(), &exportJson);
        std::printf("--- export-instance (status %d) ---\n", int(exportStatus));
        char exportId[128];
        std::snprintf(exportId, sizeof(exportId), "%s", currentId);
        if (exportJson != nullptr) {
            std::puts(exportJson);
            char taskId[32];
            if (findJsonValue(exportJson, "taskId", taskId, sizeof(taskId))) {
                char* waitJson = nullptr;
                const auracore_status waitStatus = auracore_wait_task(backend, taskId, 120000, &waitJson);
                std::printf("--- wait-export (status %d) ---\n", int(waitStatus));
                if (waitJson != nullptr) {
                    std::puts(waitJson);
                    auracore_free(waitJson);
                }
            }
            auracore_free(exportJson);
        }
        std::printf("export file exists: %s\n", QFile::exists(exportPath) ? "true" : "false");
        char* deleteJson = nullptr;
        const auracore_status deleteStatus = auracore_delete_instance(backend, currentId, &deleteJson);
        std::printf("--- delete-instance (status %d) ---\n", int(deleteStatus));
        if (deleteJson != nullptr) {
            std::puts(deleteJson);
            auracore_free(deleteJson);
        }
        printQuery("instances-after-delete", backend, auracore_list_instances);
    }
    {
        const QString importSource = QDir(dataPath).absoluteFilePath("export-test.zip");
        char* importJson = nullptr;
        const QByteArray importSourceUtf8 = importSource.toUtf8();
        const auracore_status importStatus =
            auracore_import_instance(backend, importSourceUtf8.constData(), "Aura Probe Reborn", nullptr, &importJson);
        std::printf("--- import-instance (status %d) ---\n", int(importStatus));
        if (importJson != nullptr) {
            std::puts(importJson);
            char taskId[32];
            if (findJsonValue(importJson, "taskId", taskId, sizeof(taskId))) {
                char* waitJson = nullptr;
                const auracore_status waitStatus = auracore_wait_task(backend, taskId, 180000, &waitJson);
                std::printf("--- wait-import (status %d) ---\n", int(waitStatus));
                if (waitJson != nullptr) {
                    std::puts(waitJson);
                    auracore_free(waitJson);
                }
            }
            auracore_free(importJson);
        }
        printQuery("instances-after-import", backend, auracore_list_instances);

        // Real launch e2e is opt-in: it downloads the game distribution and
        // starts a JVM, which is too heavy for the CI smoke job.
        if (std::getenv("AURACORE_PROBE_LAUNCH") != nullptr) {
            char* launchAccountJson = nullptr;
            auracore_add_offline_account(backend, "AuraTester", &launchAccountJson);
            if (launchAccountJson != nullptr) {
                std::puts(launchAccountJson);
                auracore_free(launchAccountJson);
            }
            char* defaultJson2 = nullptr;
            auracore_set_default_account(backend, "AuraTester", &defaultJson2);
            if (defaultJson2 != nullptr) {
                auracore_free(defaultJson2);
            }

            char* launchJson = nullptr;
            const auracore_status launchStatus = auracore_launch_instance(backend, "Aura Probe Reborn", "AuraTester", nullptr, &launchJson);
            std::printf("--- launch-instance (status %d) ---\n", int(launchStatus));
            char launchTaskId[32] = "";
            if (launchJson != nullptr) {
                std::puts(launchJson);
                findJsonValue(launchJson, "taskId", launchTaskId, sizeof(launchTaskId));
                auracore_free(launchJson);
            }
            if (launchTaskId[0] != '\0') {
                // A launch task stays alive for the whole game session, so the
                // probe only gives the update/auth chain time to reach the
                // process spawn, reports the snapshot, then stops the game.
                char* waitJson = nullptr;
                const auracore_status waitStatus = auracore_wait_task(backend, launchTaskId, 150000, &waitJson);
                std::printf("--- wait-launch (status %d) ---\n", int(waitStatus));
                if (waitJson != nullptr) {
                    std::puts(waitJson);
                    auracore_free(waitJson);
                }
                char* statusJson = nullptr;
                auracore_task_status(backend, launchTaskId, &statusJson);
                std::printf("--- launch-status ---\n");
                if (statusJson != nullptr) {
                    std::puts(statusJson);
                    auracore_free(statusJson);
                }
            }
            char* stopJson = nullptr;
            const auracore_status stopStatus = auracore_stop_instance(backend, "Aura Probe Reborn", &stopJson);
            std::printf("--- stop-instance (status %d) ---\n", int(stopStatus));
            if (stopJson != nullptr) {
                std::puts(stopJson);
                auracore_free(stopJson);
            }
            char* removeAccountJson = nullptr;
            auracore_remove_account(backend, "AuraTester", &removeAccountJson);
            if (removeAccountJson != nullptr) {
                auracore_free(removeAccountJson);
            }
        }
        char* cleanupJson = nullptr;
        const auracore_status cleanupStatus = auracore_delete_instance(backend, "Aura Probe Reborn", &cleanupJson);
        std::printf("--- cleanup-delete (status %d) ---\n", int(cleanupStatus));
        if (cleanupJson != nullptr) {
            std::puts(cleanupJson);
            auracore_free(cleanupJson);
        }
        printQuery("instances-final", backend, auracore_list_instances);
    }
    {
        printQuery("accounts-initial", backend, auracore_list_accounts);

        char* addJson = nullptr;
        const auracore_status addStatus = auracore_add_offline_account(backend, "AuraTester", &addJson);
        std::printf("--- add-offline-account (status %d) ---\n", int(addStatus));
        if (addJson != nullptr) {
            std::puts(addJson);
            auracore_free(addJson);
        }

        printQuery("accounts-after-add", backend, auracore_list_accounts);

        char* defaultJson = nullptr;
        const auracore_status defaultStatus = auracore_set_default_account(backend, "AuraTester", &defaultJson);
        std::printf("--- set-default-account (status %d) ---\n", int(defaultStatus));
        if (defaultJson != nullptr) {
            std::puts(defaultJson);
            auracore_free(defaultJson);
        }

        char* removeJson = nullptr;
        const auracore_status removeStatus = auracore_remove_account(backend, "AuraTester", &removeJson);
        std::printf("--- remove-account (status %d) ---\n", int(removeStatus));
        if (removeJson != nullptr) {
            std::puts(removeJson);
            auracore_free(removeJson);
        }

        printQuery("accounts-after-remove", backend, auracore_list_accounts);
    }
    {
        char* beginJson = nullptr;
        const auracore_status beginStatus = auracore_begin_msa_login(backend, &beginJson);
        std::printf("--- begin-msa-login (status %d) ---\n", int(beginStatus));
        char loginTaskId[32] = "";
        if (beginJson != nullptr) {
            std::puts(beginJson);
            findJsonValue(beginJson, "taskId", loginTaskId, sizeof(loginTaskId));
            auracore_free(beginJson);
        }
        if (loginTaskId[0] != '\0') {
            for (int attempt = 0; attempt < 20; ++attempt) {
                char* infoJson = nullptr;
                const auracore_status infoStatus = auracore_msa_login_info(backend, loginTaskId, &infoJson);
                std::printf("--- msa-login-info attempt %d (status %d) ---\n", attempt, int(infoStatus));
                if (infoJson != nullptr) {
                    std::puts(infoJson);
                    const bool issued = std::strstr(infoJson, "\"codeIssued\":true") != nullptr;
                    auracore_free(infoJson);
                    if (issued) {
                        break;
                    }
                }
                char* tickJson = nullptr;
                auracore_wait_task(backend, loginTaskId, 1000, &tickJson);
                if (tickJson != nullptr) {
                    auracore_free(tickJson);
                }
            }
            auracore_cancel_task(backend, loginTaskId);
            std::printf("--- msa-login cancelled ---\n");
        }
    }
    auracore_backend_destroy(backend);
    return 0;
}
