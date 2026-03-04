/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * config_loader.h — YAML configuration loader
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_CONFIG_LOADER_H
#define MC1_CONFIG_LOADER_H

#include <QString>
#include <vector>
#include "config_types.h"

namespace mc1 {

/* Load admin config + encoder slots from a YAML file.
 * Returns true on success, sets error_out on failure. */
bool loadConfig(const QString &yaml_path,
                AdminConfig &admin_out,
                std::vector<EncoderConfig> &slots_out,
                QString &error_out);

/* Save encoder slots back to a YAML file.
 * Only writes the encoder-slots section. */
bool saveSlots(const QString &yaml_path,
               const std::vector<EncoderConfig> &slot_configs,
               QString &error_out);

} // namespace mc1

#endif // MC1_CONFIG_LOADER_H
