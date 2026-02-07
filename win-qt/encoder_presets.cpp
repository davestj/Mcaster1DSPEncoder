/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * encoder_presets.cpp — Built-in encoder preset definitions
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "encoder_presets.h"
#include <QStringList>

namespace mc1 {

using C = EncoderConfig::Codec;
using T = EncoderConfig::EncoderType;
using M = EncoderConfig::EncodeMode;
using Ch = EncoderConfig::ChannelMode;

static const QVector<EncoderPreset> kPresets = {
    /* ── RADIO: MP3 ──────────────────────────────────────────────── */
    { QStringLiteral("Radio — FM Quality"),
      T::RADIO, C::MP3, 128, 5, 44100, 2, M::CBR, Ch::JOINT,
      QStringLiteral("Standard FM broadcast — 128kbps CBR joint stereo") },

    { QStringLiteral("Radio — CD Quality"),
      T::RADIO, C::MP3, 320, 2, 44100, 2, M::CBR, Ch::STEREO,
      QStringLiteral("Maximum MP3 quality — 320kbps CBR full stereo") },

    { QStringLiteral("Radio — MP3 VBR High"),
      T::RADIO, C::MP3, 0, 2, 44100, 2, M::VBR, Ch::JOINT,
      QStringLiteral("Variable bitrate ~190kbps — best quality/size ratio") },

    /* ── RADIO: AAC ──────────────────────────────────────────────── */
    { QStringLiteral("Radio — AAC Standard"),
      T::RADIO, C::AAC_LC, 128, 0, 44100, 2, M::CBR, Ch::STEREO,
      QStringLiteral("AAC-LC 128kbps — good quality, wide compatibility") },

    { QStringLiteral("Radio — AAC High"),
      T::RADIO, C::AAC_LC, 256, 0, 48000, 2, M::CBR, Ch::STEREO,
      QStringLiteral("AAC-LC 256kbps — high fidelity streaming") },

    { QStringLiteral("Radio — AAC+ Low BW"),
      T::RADIO, C::AAC_HE, 48, 0, 44100, 2, M::CBR, Ch::STEREO,
      QStringLiteral("HE-AAC v1 48kbps — SBR for low bandwidth stereo") },

    { QStringLiteral("Radio — AAC+ Mobile"),
      T::RADIO, C::AAC_HE, 64, 0, 44100, 2, M::CBR, Ch::STEREO,
      QStringLiteral("HE-AAC v1 64kbps — mobile-friendly quality") },

    { QStringLiteral("Radio — AAC++ Ultra Low"),
      T::RADIO, C::AAC_HE_V2, 32, 0, 44100, 2, M::CBR, Ch::STEREO,
      QStringLiteral("HE-AAC v2 32kbps — PS+SBR, ultra-low bandwidth") },

    { QStringLiteral("Radio — AAC-ELD Low Latency"),
      T::RADIO, C::AAC_ELD, 64, 0, 48000, 2, M::CBR, Ch::STEREO,
      QStringLiteral("AAC-ELD 64kbps — enhanced low delay for real-time") },

    /* ── RADIO: Opus ─────────────────────────────────────────────── */
    { QStringLiteral("Radio — Opus Music"),
      T::RADIO, C::OPUS, 128, 0, 48000, 2, M::VBR, Ch::STEREO,
      QStringLiteral("Opus 128kbps VBR — excellent music quality, 48kHz") },

    { QStringLiteral("Radio — Opus Voice"),
      T::RADIO, C::OPUS, 32, 0, 48000, 1, M::VBR, Ch::MONO,
      QStringLiteral("Opus 32kbps VBR mono — optimized for speech") },

    /* ── RADIO: Vorbis ───────────────────────────────────────────── */
    { QStringLiteral("Radio — Vorbis Standard"),
      T::RADIO, C::VORBIS, 0, 5, 44100, 2, M::VBR, Ch::STEREO,
      QStringLiteral("Vorbis quality 5 (~128kbps VBR) — standard quality") },

    { QStringLiteral("Radio — Vorbis High"),
      T::RADIO, C::VORBIS, 0, 8, 44100, 2, M::VBR, Ch::STEREO,
      QStringLiteral("Vorbis quality 8 (~256kbps VBR) — high fidelity") },

    /* ── RADIO: FLAC ─────────────────────────────────────────────── */
    { QStringLiteral("Radio — FLAC Archival"),
      T::RADIO, C::FLAC, 0, 5, 44100, 2, M::CBR, Ch::STEREO,
      QStringLiteral("FLAC compression 5 — lossless, balanced speed/size") },

    /* ── PODCAST ─────────────────────────────────────────────────── */
    { QStringLiteral("Podcast — MP3 VBR High"),
      T::PODCAST, C::MP3, 0, 2, 44100, 2, M::VBR, Ch::JOINT,
      QStringLiteral("VBR ~190kbps — best quality/size for podcast audio") },

    { QStringLiteral("Podcast — AAC-LC 192k"),
      T::PODCAST, C::AAC_LC, 192, 0, 44100, 2, M::CBR, Ch::STEREO,
      QStringLiteral("AAC-LC 192kbps stereo — high quality podcast") },

    { QStringLiteral("Podcast — Opus 128k"),
      T::PODCAST, C::OPUS, 128, 0, 48000, 2, M::VBR, Ch::STEREO,
      QStringLiteral("Opus 128kbps — excellent quality, modern codec") },

    { QStringLiteral("Podcast — FLAC Lossless"),
      T::PODCAST, C::FLAC, 0, 5, 48000, 2, M::CBR, Ch::STEREO,
      QStringLiteral("Lossless — for archival master recordings") },

    { QStringLiteral("Podcast — HE-AAC Mono"),
      T::PODCAST, C::AAC_HE, 64, 0, 44100, 1, M::CBR, Ch::MONO,
      QStringLiteral("HE-AAC 64kbps mono — optimized for speech content") },

    { QStringLiteral("Podcast — MP3 64k Mono"),
      T::PODCAST, C::MP3, 64, 5, 22050, 1, M::CBR, Ch::MONO,
      QStringLiteral("Speech/talk — 64kbps mono, low bandwidth") },

    /* ── TV/VIDEO ────────────────────────────────────────────────── */
    { QStringLiteral("Video — H.264 720p 2.5Mbps"),
      T::TV_VIDEO, C::MP3, 128, 5, 44100, 2, M::CBR, Ch::STEREO,
      QStringLiteral("H.264/FLV 1280x720 30fps — standard HD streaming") },

    { QStringLiteral("Video — H.264 1080p 5Mbps"),
      T::TV_VIDEO, C::AAC_LC, 192, 0, 48000, 2, M::CBR, Ch::STEREO,
      QStringLiteral("H.264/FLV 1920x1080 30fps — full HD streaming") },

    { QStringLiteral("Video — Theora 720p"),
      T::TV_VIDEO, C::VORBIS, 0, 5, 44100, 2, M::VBR, Ch::STEREO,
      QStringLiteral("Theora/OGG 1280x720 30fps — native Icecast/DNAS") },

    { QStringLiteral("Video — VP9 WebM 720p"),
      T::TV_VIDEO, C::OPUS, 128, 0, 48000, 2, M::VBR, Ch::STEREO,
      QStringLiteral("VP9/WebM 1280x720 30fps — modern efficient codec") },

    { QStringLiteral("Video — Low Bandwidth 480p"),
      T::TV_VIDEO, C::MP3, 96, 5, 44100, 2, M::CBR, Ch::STEREO,
      QStringLiteral("H.264/FLV 854x480 24fps 1Mbps — constrained bandwidth") },
};

const QVector<EncoderPreset>& builtinPresets()
{
    return kPresets;
}

const EncoderPreset* findPreset(const QString &name)
{
    for (const auto &p : kPresets) {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

void applyPreset(const EncoderPreset &preset, EncoderConfig &cfg)
{
    cfg.encoder_type = preset.type;
    cfg.codec        = preset.codec;
    cfg.bitrate_kbps = preset.bitrate_kbps;
    cfg.quality      = preset.quality;
    cfg.sample_rate  = preset.sample_rate;
    cfg.channels     = preset.channels;
    cfg.encode_mode  = preset.encode_mode;
    cfg.channel_mode = preset.channel_mode;
}

QStringList presetsForCodec(EncoderConfig::Codec codec)
{
    QStringList result;
    for (const auto &p : kPresets) {
        if (p.codec == codec)
            result << p.name;
    }
    return result;
}

QStringList presetsForType(EncoderConfig::EncoderType type)
{
    QStringList result;
    for (const auto &p : kPresets) {
        if (p.type == type)
            result << p.name;
    }
    return result;
}

QStringList allPresetNames()
{
    QStringList result;
    result.reserve(kPresets.size());
    for (const auto &p : kPresets)
        result << p.name;
    return result;
}

} // namespace mc1
