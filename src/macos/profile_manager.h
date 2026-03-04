/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * profile_manager.h — Per-slot YAML config profile manager
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_PROFILE_MANAGER_H
#define MC1_PROFILE_MANAGER_H

#include <string>
#include <vector>
#include "config_types.h"

namespace mc1 {

class ProfileManager {
public:
    /* Returns ~/Library/Application Support/Mcaster1/profiles/ */
    static std::string profiles_dir();

    /* Save an encoder config to its own YAML file.
       Sets cfg.config_file_path. Returns written path, or empty on error. */
    static std::string save_profile(EncoderConfig& cfg);

    /* Load an encoder config from a YAML profile file. */
    static bool load_profile(const std::string& path, EncoderConfig& cfg);

    /* Scan the profiles directory and return all valid configs. */
    static std::vector<EncoderConfig> scan_profiles();

    /* Delete a profile YAML file. */
    static bool delete_profile(const std::string& path);

    /* Generate filename: encoder_{type}_{NN}.yaml
       If cfg already has a valid new-format path, reuse its NN.
       Otherwise, auto-increment by scanning existing files. */
    static std::string profile_filename(const EncoderConfig& cfg);

    /* Find next available NN for a given encoder type. */
    static int next_available_number(EncoderConfig::EncoderType type);
};

} // namespace mc1

#endif // MC1_PROFILE_MANAGER_H
