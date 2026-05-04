/*
 * Mcaster1DSPEncoder — Plugin Loader
 * dsp/plugin_loader.h
 *
 * We scan plugin directories for .so files, load them via dlopen/dlsym,
 * validate the API version, and expose loaded plugin metadata for the
 * effects rack and HTTP API.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "mc1_plugin_api.h"

#include <string>
#include <vector>
#include <mutex>
#include <functional>

namespace mc1dsp {

/* ══════════════════════════════════════════════════════════════════════════════
 * Function pointer types for resolved plugin symbols
 * ══════════════════════════════════════════════════════════════════════════════ */
using fn_plugin_info_t    = mc1_plugin_info_t    (*)();
using fn_plugin_params_t  = mc1_param_desc_t*    (*)(int*);
using fn_plugin_create_t  = mc1_plugin_handle_t  (*)(int, int);
using fn_plugin_destroy_t = void                 (*)(mc1_plugin_handle_t);
using fn_plugin_process_t = void                 (*)(mc1_plugin_handle_t, float*, size_t, int);
using fn_plugin_set_t     = void                 (*)(mc1_plugin_handle_t, const char*, float);
using fn_plugin_get_t     = float                (*)(mc1_plugin_handle_t, const char*);
using fn_plugin_reset_t   = void                 (*)(mc1_plugin_handle_t);

/* ══════════════════════════════════════════════════════════════════════════════
 * LoadedPlugin — represents one successfully loaded .so plugin
 * ══════════════════════════════════════════════════════════════════════════════ */
struct LoadedPlugin {
    void*               dl_handle = nullptr;   /* dlopen handle */
    mc1_plugin_info_t   info{};                /* metadata from mc1_plugin_info() */
    mc1_param_desc_t*   params = nullptr;      /* parameter descriptors */
    int                 param_count = 0;
    std::string         path;                  /* filesystem path to the .so */
    bool                enabled = true;        /* admin can disable without unloading */

    /* Resolved function pointers */
    fn_plugin_create_t  fn_create  = nullptr;
    fn_plugin_destroy_t fn_destroy = nullptr;
    fn_plugin_process_t fn_process = nullptr;
    fn_plugin_set_t     fn_set     = nullptr;
    fn_plugin_get_t     fn_get     = nullptr;
    fn_plugin_reset_t   fn_reset   = nullptr;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * PluginLoader — singleton that manages plugin discovery, loading, unloading
 * ══════════════════════════════════════════════════════════════════════════════ */
class PluginLoader {
public:
    /* We get the singleton instance */
    static PluginLoader& instance();

    /* We scan all plugin directories and load any new .so files found.
     * Returns the number of newly loaded plugins. */
    int scan_plugins();

    /* We load a single plugin by path. Returns pointer to the loaded plugin
     * or nullptr if loading failed. Pointer valid until unload_all(). */
    LoadedPlugin* load(const std::string& path);

    /* We unload a single plugin by type_id */
    bool unload(const std::string& type_id);

    /* We unload all plugins */
    void unload_all();

    /* We return a snapshot of all loaded plugins (thread-safe copy) */
    std::vector<LoadedPlugin> get_all() const;

    /* We find a loaded plugin by type_id. Returns nullptr if not found.
     * WARNING: Returned pointer is only valid while holding no lock and
     * no concurrent unload is happening. For audio thread use, copy the
     * function pointers you need. */
    const LoadedPlugin* find(const std::string& type_id) const;

    /* We set the enabled state of a plugin by type_id */
    bool set_enabled(const std::string& type_id, bool enabled);

    /* We return the list of directories we scan */
    static std::vector<std::string> plugin_directories();

    ~PluginLoader();

private:
    PluginLoader() = default;
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    std::vector<LoadedPlugin> plugins_;
    mutable std::mutex mtx_;

    /* We resolve all required symbols from a dlopen handle.
     * Returns false if any symbol is missing. */
    bool resolve_symbols(LoadedPlugin& p);
};

} // namespace mc1dsp
