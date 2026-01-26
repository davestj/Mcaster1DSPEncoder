// config_loader.h — Startup YAML → gAdminConfig, MySQL → pipeline slots
//
// Two-phase startup:
//   1. mc1_load_config()       — parse minimal YAML (HTTP, DB, DNAS, log)
//   2. mc1_load_slots_from_db() — query encoder_configs table → pipeline slots
//
// CLI flags always override YAML values — apply after mc1_load_config().
#pragma once

#include <string>

class AudioPipeline;

// ---------------------------------------------------------------------------
// mc1_load_config
//
// Parses the startup YAML into gAdminConfig (HTTP sockets, admin credentials,
// database connection, DNAS proxy target, log settings, webroot).
// Does NOT load encoder slots — use mc1_load_slots_from_db() for that.
//
// Returns true on success.
// ---------------------------------------------------------------------------
bool mc1_load_config(const char*    path,
                     AudioPipeline* pipeline,
                     std::string*   webroot_out = nullptr);

// ---------------------------------------------------------------------------
// mc1_load_slots_from_db
//
// Connects to MySQL using gAdminConfig.db credentials and loads all active
// rows from encoder_configs, calling pipeline->add_slot() for each one.
// Must be called after mc1_load_config() so db credentials are available.
//
// Returns true if at least one slot was loaded successfully.
// ---------------------------------------------------------------------------
bool mc1_load_slots_from_db(AudioPipeline* pipeline);
