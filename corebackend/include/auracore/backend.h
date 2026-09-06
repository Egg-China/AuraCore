// SPDX-License-Identifier: GPL-3.0-only
/*
 *  AuraCore - stable C ABI for the headless launcher core
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

#ifndef AURACORE_BACKEND_H
#define AURACORE_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(AURACORE_BACKEND_BUILDING)
#define AURACORE_BACKEND_API __declspec(dllexport)
#else
#define AURACORE_BACKEND_API __declspec(dllimport)
#endif
#else
#define AURACORE_BACKEND_API __attribute__((visibility("default")))
#endif

/* Bump whenever the meaning of existing entry points changes.
 * Additive changes only bump the minor revision. */
#define AURACORE_ABI_VERSION 0

typedef struct auracore_backend auracore_backend;

typedef enum auracore_status {
    AURACORE_OK = 0,
    AURACORE_ERROR_INVALID_ARGUMENT = 1,
    AURACORE_ERROR_BACKEND = 2,
    AURACORE_ERROR_OUT_OF_MEMORY = 3
} auracore_status;

/* Returns the ABI revision implemented by this library. */
AURACORE_BACKEND_API int auracore_abi_version(void);

/* Creates a backend bound to a writable data directory. A QCoreApplication
 * (or QApplication) must already exist and calls must stay on that thread.
 * The directory is created if missing. */
AURACORE_BACKEND_API auracore_status auracore_backend_create(const char* data_path, auracore_backend** out_backend);

/* Releases the backend and all owned core services. */
AURACORE_BACKEND_API void auracore_backend_destroy(auracore_backend* backend);

/* Human readable message for the most recent failure on this backend.
 * Owned by the backend; valid until the next call on the same backend. */
AURACORE_BACKEND_API const char* auracore_last_error(auracore_backend* backend);

/* Enumerates locally known instances as a compact JSON array of
 * { id, name, dir, icon, lastLaunch, gameVersion } objects. */
AURACORE_BACKEND_API auracore_status auracore_list_instances(auracore_backend* backend, char** out_json);

/* Returns metadata for one instance as a JSON object, or
 * AURACORE_ERROR_BACKEND when the id is unknown. */
AURACORE_BACKEND_API auracore_status auracore_get_instance(auracore_backend* backend, const char* instance_id, char** out_json);

/* Scans the local system for Java executables (PATH, registry, common
 * install roots) and returns a JSON array of { path } candidates. */
AURACORE_BACKEND_API auracore_status auracore_detect_java(auracore_backend* backend, char** out_json);

/* Returns cached component metadata (meta/index.json) as
 * { cached: bool, lists: [ { uid, name, versions: [...] } ] }.
 * No network access is performed. */
AURACORE_BACKEND_API auracore_status auracore_list_component_lists(auracore_backend* backend, char** out_json);

/* Runs the real Java executable behind every detected candidate and returns
 * { path, version, vendor, arch } entries (or { path, error } when the
 * check fails). Requires JavaCheck.jar, discoverable through the
 * AURACORE_JARS_DIR environment override or the core's default jar paths. */
AURACORE_BACKEND_API auracore_status auracore_probe_java(auracore_backend* backend, char** out_json);

/* Returns the cached version list of one component uid as
 * { uid, cached: bool, versions: [...] }. No network access is performed. */
AURACORE_BACKEND_API auracore_status auracore_list_component_versions(auracore_backend* backend, const char* uid, char** out_json);
/* Refreshes meta/index.json from the network (bounded by a two minute
 * safety valve). Returns { refreshed, lists, error? }; failures do not
 * throw a backend status so hosts can treat offline mode as data. */
AURACORE_BACKEND_API auracore_status auracore_refresh_metadata(auracore_backend* backend, char** out_json);

/* Refreshes one component's version list (meta/<uid>/index.json). */
AURACORE_BACKEND_API auracore_status auracore_refresh_component(auracore_backend* backend, const char* uid, char** out_json);
/* Starts creation of a vanilla instance and returns { created, taskId, ... }
 * or { created: false, error }. group may be NULL. The task runs on the
 * core's event loop; poll with auracore_task_status, wait synchronously with
 * auracore_wait_task, or cancel through auracore_cancel_task. */
AURACORE_BACKEND_API auracore_status auracore_create_instance(auracore_backend* backend,
                                                              const char* name,
                                                              const char* game_version,
                                                              const char* group,
                                                              char** out_json);

/* Returns { id, type, state, progress, total, status, succeeded?, error? }. */
AURACORE_BACKEND_API auracore_status auracore_task_status(auracore_backend* backend, const char* task_id, char** out_json);

/* Pumps the core event loop until the task finishes or timeout_ms elapses,
 * then returns the final status snapshot. */
AURACORE_BACKEND_API auracore_status auracore_wait_task(auracore_backend* backend, const char* task_id, int timeout_ms, char** out_json);

/* Requests cancellation; returns OK when the task accepted the abort. */
AURACORE_BACKEND_API auracore_status auracore_cancel_task(auracore_backend* backend, const char* task_id);
/* Renames an instance: updates instance.cfg and, when the physical directory
 * can be renamed, also moves the directory and reloads the instance list.
 * Returns { renamed, oldId, id, name, dirRenamed }. */
AURACORE_BACKEND_API auracore_status auracore_rename_instance(auracore_backend* backend, const char* id, const char* new_name, char** out_json);

/* Moves an instance between groups; an empty string clears the group. */
AURACORE_BACKEND_API auracore_status auracore_set_instance_group(auracore_backend* backend, const char* id, const char* group, char** out_json);

/* Sets the icon key used by launcher UIs. */
AURACORE_BACKEND_API auracore_status auracore_set_instance_icon(auracore_backend* backend, const char* id, const char* icon_key, char** out_json);

/* Deletes the instance directory, its shortcuts, and its group membership.
 * Returns { deleted, id } or { deleted: false, error }. */
AURACORE_BACKEND_API auracore_status auracore_delete_instance(auracore_backend* backend, const char* id, char** out_json);
/* Starts a MultiMC-format zip export of the instance directory. Poll the
 * returned taskId with auracore_task_status / auracore_wait_task. */
AURACORE_BACKEND_API auracore_status auracore_export_instance(auracore_backend* backend, const char* id, const char* output_path, char** out_json);

/* Starts an import from a local archive path or http(s) URL. MultiMC,
 * Modrinth, CurseForge and Technic archives are auto-detected. group may be
 * NULL. Returns { importing, taskId, name, source }. */
AURACORE_BACKEND_API auracore_status auracore_import_instance(auracore_backend* backend, const char* source, const char* name, const char* group, char** out_json);
/* Lists stored accounts as [{ profileName, type, internalId, hasProfile }]. */
AURACORE_BACKEND_API auracore_status auracore_list_accounts(auracore_backend* backend, char** out_json);

/* Creates and stores an offline profile; rejects duplicate profile names. */
AURACORE_BACKEND_API auracore_status auracore_add_offline_account(auracore_backend* backend, const char* username, char** out_json);

/* Removes an account by its profile name. */
AURACORE_BACKEND_API auracore_status auracore_remove_account(auracore_backend* backend, const char* profile_name, char** out_json);

/* Selects the account future launches use by default. */
AURACORE_BACKEND_API auracore_status auracore_set_default_account(auracore_backend* backend, const char* profile_name, char** out_json);
/* Frees a string produced by any query above. NULL is accepted. */
AURACORE_BACKEND_API void auracore_free(char* text);

#ifdef __cplusplus
}
#endif

#endif /* AURACORE_BACKEND_H */