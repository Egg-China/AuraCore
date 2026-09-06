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
#include <QTemporaryDir>

#include <cstdio>
#include <cstring>

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
    auracore_backend_destroy(backend);
    return 0;
}
