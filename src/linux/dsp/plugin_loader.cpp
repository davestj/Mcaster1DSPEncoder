/*
 * Mcaster1DSPEncoder — Plugin Loader Implementation
 * dsp/plugin_loader.cpp
 *
 * We scan plugin directories for .so files, validate API version,
 * resolve symbols, and manage the plugin lifecycle.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "plugin_loader.h"
#include "../mc1_logger.h"

#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <cstring>
#include <algorithm>

namespace mc1dsp {

/* ══════════════════════════════════════════════════════════════════════════════
 * Singleton
 * ══════════════════════════════════════════════════════════════════════════════ */

PluginLoader& PluginLoader::instance() {
    static PluginLoader inst;
    return inst;
}

PluginLoader::~PluginLoader() {
    unload_all();
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Plugin directories we scan (in priority order)
 * ══════════════════════════════════════════════════════════════════════════════ */

std::vector<std::string> PluginLoader::plugin_directories() {
    std::vector<std::string> dirs;

    /* System-wide install paths */
    dirs.push_back("/usr/lib/mcaster1/plugins");
    dirs.push_back("/usr/local/lib/mcaster1/plugins");

    /* Per-user plugin directory */
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (home) {
        dirs.push_back(std::string(home) + "/.mcaster1/plugins");
    }

    /* Project-local plugins directory (for development) */
    dirs.push_back("plugins");

    return dirs;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Helper: check if a path ends with ".so"
 * ══════════════════════════════════════════════════════════════════════════════ */
static bool ends_with_so(const std::string& path) {
    if (path.size() < 3) return false;
    return path.compare(path.size() - 3, 3, ".so") == 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Helper: check if a type_id is already loaded
 * ══════════════════════════════════════════════════════════════════════════════ */
static bool is_already_loaded(const std::vector<LoadedPlugin>& plugins, const std::string& type_id) {
    for (const auto& p : plugins) {
        if (p.info.type_id && type_id == p.info.type_id) return true;
    }
    return false;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Resolve all required function symbols from a dlopen handle
 * ══════════════════════════════════════════════════════════════════════════════ */

bool PluginLoader::resolve_symbols(LoadedPlugin& p) {
    /* We clear dlerror before each dlsym to get accurate error messages */
    dlerror();

    auto info_fn = (fn_plugin_info_t)dlsym(p.dl_handle, "mc1_plugin_info");
    if (!info_fn) {
        MC1_WARN("Plugin " + p.path + ": missing mc1_plugin_info: " + std::string(dlerror() ? dlerror() : ""));
        return false;
    }

    auto params_fn = (fn_plugin_params_t)dlsym(p.dl_handle, "mc1_plugin_params");
    if (!params_fn) {
        MC1_WARN("Plugin " + p.path + ": missing mc1_plugin_params");
        return false;
    }

    p.fn_create  = (fn_plugin_create_t)dlsym(p.dl_handle, "mc1_plugin_create");
    p.fn_destroy = (fn_plugin_destroy_t)dlsym(p.dl_handle, "mc1_plugin_destroy");
    p.fn_process = (fn_plugin_process_t)dlsym(p.dl_handle, "mc1_plugin_process");
    p.fn_set     = (fn_plugin_set_t)dlsym(p.dl_handle, "mc1_plugin_set_param");
    p.fn_get     = (fn_plugin_get_t)dlsym(p.dl_handle, "mc1_plugin_get_param");
    p.fn_reset   = (fn_plugin_reset_t)dlsym(p.dl_handle, "mc1_plugin_reset");

    if (!p.fn_create || !p.fn_destroy || !p.fn_process ||
        !p.fn_set || !p.fn_get || !p.fn_reset) {
        MC1_WARN("Plugin " + p.path + ": missing one or more required symbols");
        return false;
    }

    /* We call mc1_plugin_info() to get metadata */
    p.info = info_fn();

    /* We validate API version */
    if (p.info.api_version != MC1_PLUGIN_API_VERSION) {
        MC1_WARN("Plugin " + p.path + ": API version mismatch (got " +
                 std::to_string(p.info.api_version) + ", expected " +
                 std::to_string(MC1_PLUGIN_API_VERSION) + ")");
        return false;
    }

    /* We validate required metadata fields */
    if (!p.info.type_id || strlen(p.info.type_id) == 0) {
        MC1_WARN("Plugin " + p.path + ": empty type_id");
        return false;
    }
    if (!p.info.display_name) p.info.display_name = p.info.type_id;
    if (!p.info.version)      p.info.version = "0.0.0";
    if (!p.info.author)       p.info.author = "Unknown";
    if (!p.info.description)  p.info.description = "";

    /* We get parameter descriptors */
    p.params = params_fn(&p.param_count);
    if (p.param_count < 0) p.param_count = 0;

    return true;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Load a single plugin by path
 * ══════════════════════════════════════════════════════════════════════════════ */

LoadedPlugin* PluginLoader::load(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);

    /* We check if this path is already loaded */
    for (auto& p : plugins_) {
        if (p.path == path) {
            MC1_DBG("Plugin already loaded: " + path);
            return &p;
        }
    }

    /* We open the shared library with RTLD_NOW to catch missing symbols early.
     * RTLD_LOCAL prevents plugin symbols from polluting the global namespace. */
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        MC1_WARN("Failed to load plugin " + path + ": " + std::string(dlerror() ? dlerror() : "unknown error"));
        return nullptr;
    }

    LoadedPlugin p;
    p.dl_handle = handle;
    p.path = path;

    if (!resolve_symbols(p)) {
        dlclose(handle);
        return nullptr;
    }

    /* We check for duplicate type_id */
    std::string tid(p.info.type_id);
    if (is_already_loaded(plugins_, tid)) {
        MC1_WARN("Plugin type_id '" + tid + "' already loaded, skipping " + path);
        dlclose(handle);
        return nullptr;
    }

    MC1_INFO("Loaded plugin: " + std::string(p.info.display_name) +
             " v" + std::string(p.info.version) +
             " (" + tid + ") from " + path);

    plugins_.push_back(std::move(p));
    return &plugins_.back();
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Scan all plugin directories
 * ══════════════════════════════════════════════════════════════════════════════ */

int PluginLoader::scan_plugins() {
    auto dirs = plugin_directories();
    int loaded = 0;

    for (const auto& dir : dirs) {
        struct stat st;
        if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;  /* Directory does not exist — skip silently */
        }

        DIR* dp = opendir(dir.c_str());
        if (!dp) continue;

        MC1_DBG("Scanning plugin directory: " + dir);

        struct dirent* entry;
        while ((entry = readdir(dp)) != nullptr) {
            std::string name(entry->d_name);
            if (!ends_with_so(name)) continue;

            std::string fullpath = dir + "/" + name;

            /* We check if already loaded by path (without lock — load() will lock) */
            bool already = false;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                for (const auto& p : plugins_) {
                    if (p.path == fullpath) { already = true; break; }
                }
            }
            if (already) continue;

            if (load(fullpath)) {
                ++loaded;
            }
        }

        closedir(dp);
    }

    if (loaded > 0) {
        MC1_INFO("Plugin scan complete: " + std::to_string(loaded) + " new plugin(s) loaded");
    }

    return loaded;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Unload a single plugin by type_id
 * ══════════════════════════════════════════════════════════════════════════════ */

bool PluginLoader::unload(const std::string& type_id) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = plugins_.begin(); it != plugins_.end(); ++it) {
        if (it->info.type_id && type_id == it->info.type_id) {
            MC1_INFO("Unloading plugin: " + type_id);
            if (it->dl_handle) {
                dlclose(it->dl_handle);
            }
            plugins_.erase(it);
            return true;
        }
    }
    return false;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Unload all plugins
 * ══════════════════════════════════════════════════════════════════════════════ */

void PluginLoader::unload_all() {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& p : plugins_) {
        if (p.dl_handle) {
            MC1_INFO("Unloading plugin: " + std::string(p.info.type_id ? p.info.type_id : "?"));
            dlclose(p.dl_handle);
            p.dl_handle = nullptr;
        }
    }
    plugins_.clear();
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Get all loaded plugins (thread-safe copy)
 * ══════════════════════════════════════════════════════════════════════════════ */

std::vector<LoadedPlugin> PluginLoader::get_all() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return plugins_;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Find a loaded plugin by type_id
 * ══════════════════════════════════════════════════════════════════════════════ */

const LoadedPlugin* PluginLoader::find(const std::string& type_id) const {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& p : plugins_) {
        if (p.info.type_id && type_id == p.info.type_id) {
            return &p;
        }
    }
    return nullptr;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Enable/disable a plugin by type_id
 * ══════════════════════════════════════════════════════════════════════════════ */

bool PluginLoader::set_enabled(const std::string& type_id, bool enabled) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& p : plugins_) {
        if (p.info.type_id && type_id == p.info.type_id) {
            p.enabled = enabled;
            MC1_INFO("Plugin " + type_id + (enabled ? " enabled" : " disabled"));
            return true;
        }
    }
    return false;
}

} // namespace mc1dsp
