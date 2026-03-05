/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * codec_validator.cpp — Per-codec parameter validation
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "codec_validator.h"
#include <algorithm>

namespace mc1 {

static bool is_valid_sr(int sr, const int* list, int count)
{
    for (int i = 0; i < count; ++i)
        if (list[i] == sr) return true;
    return false;
}

CodecValidation validate_codec_params(EncoderConfig::Codec codec,
                                      int bitrate_kbps,
                                      int sample_rate,
                                      int channels)
{
    CodecValidation v;
    v.suggested_bitrate     = bitrate_kbps;
    v.suggested_sample_rate = sample_rate;
    v.suggested_channels    = channels;

    switch (codec) {

    case EncoderConfig::Codec::MP3: {
        static const int valid_sr[] = {22050, 32000, 44100, 48000};
        if (!is_valid_sr(sample_rate, valid_sr, 4)) {
            v.valid = false;
            v.error = "MP3: sample rate must be 22050, 32000, 44100, or 48000 Hz";
            v.suggested_sample_rate = 44100;
        }
        if (bitrate_kbps < 32 || bitrate_kbps > 320) {
            v.valid = false;
            v.error = "MP3: bitrate must be 32-320 kbps";
            v.suggested_bitrate = std::clamp(bitrate_kbps, 32, 320);
        }
        if (channels < 1 || channels > 2) {
            v.valid = false;
            v.error = "MP3: channels must be 1 or 2";
            v.suggested_channels = 2;
        }
        break;
    }

    case EncoderConfig::Codec::VORBIS: {
        static const int valid_sr[] = {22050, 44100, 48000};
        if (!is_valid_sr(sample_rate, valid_sr, 3)) {
            v.valid = false;
            v.error = "Vorbis: sample rate must be 22050, 44100, or 48000 Hz";
            v.suggested_sample_rate = 44100;
        }
        if (channels < 1 || channels > 2) {
            v.valid = false;
            v.error = "Vorbis: channels must be 1 or 2";
            v.suggested_channels = 2;
        }
        break;
    }

    case EncoderConfig::Codec::OPUS: {
        /* Opus resamples internally to 48kHz — any input sample rate is fine */
        if (bitrate_kbps < 6 || bitrate_kbps > 510) {
            v.valid = false;
            v.error = "Opus: bitrate must be 6-510 kbps";
            v.suggested_bitrate = std::clamp(bitrate_kbps, 6, 510);
        }
        if (channels < 1 || channels > 2) {
            v.valid = false;
            v.error = "Opus: channels must be 1 or 2";
            v.suggested_channels = 2;
        }
        break;
    }

    case EncoderConfig::Codec::FLAC: {
        static const int valid_sr[] = {44100, 48000, 88200, 96000};
        if (!is_valid_sr(sample_rate, valid_sr, 4)) {
            v.valid = false;
            v.error = "FLAC: sample rate must be 44100, 48000, 88200, or 96000 Hz";
            v.suggested_sample_rate = 44100;
        }
        if (channels < 1 || channels > 8) {
            v.valid = false;
            v.error = "FLAC: channels must be 1-8";
            v.suggested_channels = 2;
        }
        break;
    }

    case EncoderConfig::Codec::AAC_LC: {
        static const int valid_sr[] = {22050, 32000, 44100, 48000};
        if (!is_valid_sr(sample_rate, valid_sr, 4)) {
            v.valid = false;
            v.error = "AAC-LC: sample rate must be 22050, 32000, 44100, or 48000 Hz";
            v.suggested_sample_rate = 44100;
        }
        if (bitrate_kbps < 64 || bitrate_kbps > 320) {
            v.valid = false;
            v.error = "AAC-LC: bitrate must be 64-320 kbps";
            v.suggested_bitrate = std::clamp(bitrate_kbps, 64, 320);
        }
        if (channels < 1 || channels > 2) {
            v.valid = false;
            v.error = "AAC-LC: channels must be 1 or 2";
            v.suggested_channels = 2;
        }
        break;
    }

    case EncoderConfig::Codec::AAC_HE: {
        /* HE-AAC v1 (SBR) — lower bitrates than LC */
        static const int valid_sr[] = {32000, 44100, 48000};
        if (!is_valid_sr(sample_rate, valid_sr, 3)) {
            v.valid = false;
            v.error = "HE-AAC v1: sample rate must be 32000, 44100, or 48000 Hz";
            v.suggested_sample_rate = 44100;
        }
        if (bitrate_kbps < 24 || bitrate_kbps > 128) {
            v.valid = false;
            v.error = "HE-AAC v1: bitrate must be 24-128 kbps";
            v.suggested_bitrate = std::clamp(bitrate_kbps, 24, 128);
        }
        if (channels < 1 || channels > 2) {
            v.valid = false;
            v.error = "HE-AAC v1: channels must be 1 or 2";
            v.suggested_channels = 2;
        }
        break;
    }

    case EncoderConfig::Codec::AAC_HE_V2: {
        /* HE-AAC v2 (SBR+PS) — MUST be stereo, very low bitrates */
        static const int valid_sr[] = {32000, 44100, 48000};
        if (!is_valid_sr(sample_rate, valid_sr, 3)) {
            v.valid = false;
            v.error = "HE-AAC v2: sample rate must be 32000, 44100, or 48000 Hz";
            v.suggested_sample_rate = 44100;
        }
        if (bitrate_kbps < 16 || bitrate_kbps > 64) {
            v.valid = false;
            v.error = "HE-AAC v2: bitrate must be 16-64 kbps";
            v.suggested_bitrate = std::clamp(bitrate_kbps, 16, 64);
        }
        if (channels != 2) {
            v.valid = false;
            v.error = "HE-AAC v2: channels MUST be 2 (Parametric Stereo requires stereo input)";
            v.suggested_channels = 2;
        }
        break;
    }

    case EncoderConfig::Codec::AAC_ELD: {
        /* AAC-ELD (Enhanced Low Delay) — low latency, SBR-ELD */
        static const int valid_sr[] = {32000, 44100, 48000};
        if (!is_valid_sr(sample_rate, valid_sr, 3)) {
            v.valid = false;
            v.error = "AAC-ELD: sample rate must be 32000, 44100, or 48000 Hz";
            v.suggested_sample_rate = 48000;
        }
        if (bitrate_kbps < 24 || bitrate_kbps > 192) {
            v.valid = false;
            v.error = "AAC-ELD: bitrate must be 24-192 kbps";
            v.suggested_bitrate = std::clamp(bitrate_kbps, 24, 192);
        }
        if (channels < 1 || channels > 2) {
            v.valid = false;
            v.error = "AAC-ELD: channels must be 1 or 2";
            v.suggested_channels = 2;
        }
        break;
    }

    } // switch

    return v;
}

CodecValidation validate_encoder_config(const EncoderConfig &cfg)
{
    return validate_codec_params(cfg.codec, cfg.bitrate_kbps,
                                cfg.sample_rate, cfg.channels);
}

} // namespace mc1
