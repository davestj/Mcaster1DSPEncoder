/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * codec_validator.h — Per-codec parameter validation (AAC bug fix)
 *
 * Validates encoder parameters before codec init to prevent
 * fdk-aac crashes with invalid parameter combinations.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_CODEC_VALIDATOR_H
#define MC1_CODEC_VALIDATOR_H

#include <string>
#include "config_types.h"

namespace mc1 {

struct CodecValidation {
    bool        valid = true;
    std::string error;
    int         suggested_bitrate    = 0;
    int         suggested_sample_rate = 0;
    int         suggested_channels   = 0;
};

/* Validate parameters for any codec. Returns valid=true if OK,
 * or valid=false with error message and suggested corrections. */
CodecValidation validate_codec_params(EncoderConfig::Codec codec,
                                      int bitrate_kbps,
                                      int sample_rate,
                                      int channels);

/* Convenience: validate an entire EncoderConfig */
CodecValidation validate_encoder_config(const EncoderConfig &cfg);

} // namespace mc1

#endif // MC1_CODEC_VALIDATOR_H
