/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * encoder_presets.h — Built-in encoder preset definitions
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_ENCODER_PRESETS_H
#define MC1_ENCODER_PRESETS_H

#include <QString>
#include <QVector>
#include "config_types.h"

namespace mc1 {

struct EncoderPreset {
    QString                    name;
    EncoderConfig::EncoderType type;          /* Phase M7: RADIO/PODCAST/TV_VIDEO */
    EncoderConfig::Codec       codec;
    int                        bitrate_kbps;  /* 0 = N/A (VBR/lossless) */
    int                        quality;       /* 0-10, codec-specific */
    int                        sample_rate;
    int                        channels;
    EncoderConfig::EncodeMode  encode_mode;
    EncoderConfig::ChannelMode channel_mode;
    QString                    description;
};

/* Returns all built-in presets */
const QVector<EncoderPreset>& builtinPresets();

/* Find a preset by name (returns nullptr if not found) */
const EncoderPreset* findPreset(const QString &name);

/* Apply a preset to an EncoderConfig (codec + audio params only) */
void applyPreset(const EncoderPreset &preset, EncoderConfig &cfg);

/* Return preset names for a specific codec (empty = all codecs) */
QStringList presetsForCodec(EncoderConfig::Codec codec);

/* Phase M7: Return preset names for a specific encoder type */
QStringList presetsForType(EncoderConfig::EncoderType type);

/* Return all preset names */
QStringList allPresetNames();

} // namespace mc1

#endif // MC1_ENCODER_PRESETS_H
