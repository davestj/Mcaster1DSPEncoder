/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * profile_manager.cpp — Per-slot YAML config profile manager
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "profile_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

#include <yaml.h>
#include <cstring>
#include <algorithm>
#include <cctype>

namespace mc1 {

/* ── Helpers ─────────────────────────────────────────────────────────── */

static std::string sanitize(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (c == ' ')
            out += '-';
    }
    if (out.empty()) out = "unnamed";
    return out;
}

static const char* type_tag(EncoderConfig::EncoderType t)
{
    switch (t) {
        case EncoderConfig::EncoderType::RADIO:    return "radio";
        case EncoderConfig::EncoderType::PODCAST:  return "podcast";
        case EncoderConfig::EncoderType::TV_VIDEO: return "videotv";
    }
    return "radio";
}

/* Migrate old-format encoder profiles from profiles/ subdir to the main config dir.
 * Old format: profiles/encoder_{type}_{NN}.yaml → new: {type}_encoder_{NN}.yaml */
static void migrate_old_profiles(const std::string& config_dir)
{
    std::string old_dir = config_dir + "/profiles";
    QDir old_qdir(QString::fromStdString(old_dir));
    if (!old_qdir.exists()) return;

    QStringList filters;
    filters << QStringLiteral("encoder_*.yaml");
    QStringList old_files = old_qdir.entryList(filters, QDir::Files, QDir::Name);
    if (old_files.isEmpty()) return;

    fprintf(stderr, "[M11.5] Migrating %d old profile(s) from '%s' to '%s'\n",
            static_cast<int>(old_files.size()), old_dir.c_str(), config_dir.c_str());

    for (const QString& f : old_files) {
        std::string fname = f.toStdString();
        std::string new_name = fname;

        /* Rename encoder_radio_00.yaml → radio_encoder_00.yaml */
        const char* types[] = { "radio", "podcast", "videotv" };
        for (const char* t : types) {
            std::string old_prefix = std::string("encoder_") + t + "_";
            if (fname.rfind(old_prefix, 0) == 0) {
                std::string suffix = fname.substr(old_prefix.size()); /* "00.yaml" */
                new_name = std::string(t) + "_encoder_" + suffix;
                break;
            }
        }

        QString src = QString::fromStdString(old_dir + "/" + fname);
        QString dst = QString::fromStdString(config_dir + "/" + new_name);

        /* Don't overwrite if new file already exists */
        if (QFile::exists(dst)) {
            fprintf(stderr, "[M11.5]   skip (exists): %s\n", new_name.c_str());
            continue;
        }

        if (QFile::rename(src, dst)) {
            fprintf(stderr, "[M11.5]   migrated: %s → %s\n", fname.c_str(), new_name.c_str());
        } else {
            fprintf(stderr, "[M11.5]   FAILED to migrate: %s\n", fname.c_str());
        }
    }

    /* Remove old profiles/ dir if now empty */
    if (old_qdir.entryList(QDir::Files).isEmpty()) {
        QDir().rmdir(QString::fromStdString(old_dir));
        fprintf(stderr, "[M11.5]   Removed empty profiles/ directory\n");
    }
}

static EncoderConfig::EncoderType parse_type(const std::string& s)
{
    if (s == "podcast")  return EncoderConfig::EncoderType::PODCAST;
    if (s == "tvvideo" || s == "videotv")
        return EncoderConfig::EncoderType::TV_VIDEO;
    return EncoderConfig::EncoderType::RADIO;
}

static const char* codec_tag(EncoderConfig::Codec c)
{
    switch (c) {
        case EncoderConfig::Codec::MP3:       return "mp3";
        case EncoderConfig::Codec::VORBIS:    return "vorbis";
        case EncoderConfig::Codec::OPUS:      return "opus";
        case EncoderConfig::Codec::FLAC:      return "flac";
        case EncoderConfig::Codec::AAC_LC:    return "aac_lc";
        case EncoderConfig::Codec::AAC_HE:    return "aac_he";
        case EncoderConfig::Codec::AAC_HE_V2: return "aac_hev2";
        case EncoderConfig::Codec::AAC_ELD:   return "aac_eld";
    }
    return "mp3";
}

static EncoderConfig::Codec parse_codec(const std::string& s)
{
    if (s == "vorbis")   return EncoderConfig::Codec::VORBIS;
    if (s == "opus")     return EncoderConfig::Codec::OPUS;
    if (s == "flac")     return EncoderConfig::Codec::FLAC;
    if (s == "aac_lc")   return EncoderConfig::Codec::AAC_LC;
    if (s == "aac_he")   return EncoderConfig::Codec::AAC_HE;
    if (s == "aac_hev2") return EncoderConfig::Codec::AAC_HE_V2;
    if (s == "aac_eld")  return EncoderConfig::Codec::AAC_ELD;
    return EncoderConfig::Codec::MP3;
}

static const char* encode_mode_tag(EncoderConfig::EncodeMode m)
{
    switch (m) {
        case EncoderConfig::EncodeMode::CBR: return "cbr";
        case EncoderConfig::EncodeMode::VBR: return "vbr";
        case EncoderConfig::EncodeMode::ABR: return "abr";
    }
    return "cbr";
}

static EncoderConfig::EncodeMode parse_encode_mode(const std::string& s)
{
    if (s == "vbr") return EncoderConfig::EncodeMode::VBR;
    if (s == "abr") return EncoderConfig::EncodeMode::ABR;
    return EncoderConfig::EncodeMode::CBR;
}

static const char* channel_mode_tag(EncoderConfig::ChannelMode m)
{
    switch (m) {
        case EncoderConfig::ChannelMode::JOINT:  return "joint";
        case EncoderConfig::ChannelMode::STEREO: return "stereo";
        case EncoderConfig::ChannelMode::MONO:   return "mono";
    }
    return "joint";
}

static EncoderConfig::ChannelMode parse_channel_mode(const std::string& s)
{
    if (s == "stereo") return EncoderConfig::ChannelMode::STEREO;
    if (s == "mono")   return EncoderConfig::ChannelMode::MONO;
    return EncoderConfig::ChannelMode::JOINT;
}

static const char* metadata_source_tag(MetadataConfig::Source s)
{
    switch (s) {
        case MetadataConfig::Source::MANUAL:   return "manual";
        case MetadataConfig::Source::URL:      return "url";
        case MetadataConfig::Source::FILE:     return "file";
        case MetadataConfig::Source::DISABLED: return "disabled";
    }
    return "disabled";
}

static MetadataConfig::Source parse_metadata_source(const std::string& s)
{
    if (s == "manual") return MetadataConfig::Source::MANUAL;
    if (s == "url")    return MetadataConfig::Source::URL;
    if (s == "file")   return MetadataConfig::Source::FILE;
    return MetadataConfig::Source::DISABLED;
}

static const char* protocol_tag(StreamTarget::Protocol p)
{
    switch (p) {
        case StreamTarget::Protocol::ICECAST2:      return "icecast2";
        case StreamTarget::Protocol::SHOUTCAST_V1:  return "shoutcast_v1";
        case StreamTarget::Protocol::SHOUTCAST_V2:  return "shoutcast_v2";
        case StreamTarget::Protocol::LIVE365:       return "live365";
        case StreamTarget::Protocol::MCASTER1_DNAS: return "mcaster1_dnas";
        case StreamTarget::Protocol::YOUTUBE:       return "youtube";
        case StreamTarget::Protocol::TWITCH:        return "twitch";
    }
    return "icecast2";
}

static StreamTarget::Protocol parse_protocol(const std::string& s)
{
    if (s == "shoutcast_v1")  return StreamTarget::Protocol::SHOUTCAST_V1;
    if (s == "shoutcast_v2")  return StreamTarget::Protocol::SHOUTCAST_V2;
    if (s == "live365")       return StreamTarget::Protocol::LIVE365;
    if (s == "mcaster1_dnas") return StreamTarget::Protocol::MCASTER1_DNAS;
    if (s == "youtube")       return StreamTarget::Protocol::YOUTUBE;
    if (s == "twitch")        return StreamTarget::Protocol::TWITCH;
    return StreamTarget::Protocol::ICECAST2;
}

/* ── YAML writing helpers ───────────────────────────────────────────── */

static void emit_kv(QTextStream& ts, const char* key, const std::string& val)
{
    ts << "  " << key << ": \"" << QString::fromStdString(val) << "\"\n";
}

static void emit_kv(QTextStream& ts, const char* key, int val)
{
    ts << "  " << key << ": " << val << "\n";
}

static void emit_kv(QTextStream& ts, const char* key, bool val)
{
    ts << "  " << key << ": " << (val ? "true" : "false") << "\n";
}

static void emit_kv(QTextStream& ts, const char* key, float val)
{
    ts << "  " << key << ": " << val << "\n";
}

/* Indented 4 spaces (sub-section) */
static void emit_kv4(QTextStream& ts, const char* key, float val)
{
    ts << "    " << key << ": " << val << "\n";
}
static void emit_kv4(QTextStream& ts, const char* key, bool val)
{
    ts << "    " << key << ": " << (val ? "true" : "false") << "\n";
}
static void emit_kv4(QTextStream& ts, const char* key, const char* val)
{
    ts << "    " << key << ": \"" << val << "\"\n";
}

static const char* eq_band_type_tag(mc1dsp::EqBandType t)
{
    switch (t) {
        case mc1dsp::EqBandType::LOW_SHELF:  return "low_shelf";
        case mc1dsp::EqBandType::PEAKING:    return "peaking";
        case mc1dsp::EqBandType::HIGH_SHELF: return "high_shelf";
    }
    return "peaking";
}

static mc1dsp::EqBandType parse_eq_band_type(const std::string& s)
{
    if (s == "low_shelf")  return mc1dsp::EqBandType::LOW_SHELF;
    if (s == "high_shelf") return mc1dsp::EqBandType::HIGH_SHELF;
    return mc1dsp::EqBandType::PEAKING;
}

/* ── YAML reading helpers (libyaml) ─────────────────────────────────── */

struct YamlDoc {
    yaml_document_t doc;
    bool valid = false;
    ~YamlDoc() { if (valid) yaml_document_delete(&doc); }
};

static const char* node_scalar(yaml_document_t* /*doc*/, yaml_node_t* node)
{
    if (!node || node->type != YAML_SCALAR_NODE) return "";
    return reinterpret_cast<const char*>(node->data.scalar.value);
}

static yaml_node_t* map_get(yaml_document_t* doc, yaml_node_t* map, const char* key)
{
    if (!map || map->type != YAML_MAPPING_NODE) return nullptr;
    for (auto* p = map->data.mapping.pairs.start; p < map->data.mapping.pairs.top; ++p) {
        yaml_node_t* k = yaml_document_get_node(doc, p->key);
        if (k && k->type == YAML_SCALAR_NODE &&
            strcmp(reinterpret_cast<const char*>(k->data.scalar.value), key) == 0)
            return yaml_document_get_node(doc, p->value);
    }
    return nullptr;
}

static std::string ystr(yaml_document_t* doc, yaml_node_t* map, const char* key)
{
    yaml_node_t* n = map_get(doc, map, key);
    return n ? std::string(node_scalar(doc, n)) : std::string();
}

static int yint(yaml_document_t* doc, yaml_node_t* map, const char* key, int def = 0)
{
    yaml_node_t* n = map_get(doc, map, key);
    if (!n) return def;
    const char* s = node_scalar(doc, n);
    if (!s || !*s) return def;
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    return (end != s) ? static_cast<int>(v) : def;
}

static bool ybool(yaml_document_t* doc, yaml_node_t* map, const char* key, bool def = false)
{
    yaml_node_t* n = map_get(doc, map, key);
    if (!n) return def;
    const char* s = node_scalar(doc, n);
    return (strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 || strcmp(s, "1") == 0);
}

static float yfloat(yaml_document_t* doc, yaml_node_t* map, const char* key, float def = 0.0f)
{
    yaml_node_t* n = map_get(doc, map, key);
    if (!n) return def;
    const char* s = node_scalar(doc, n);
    if (!s || !*s) return def;
    char* end = nullptr;
    float v = strtof(s, &end);
    return (end != s) ? v : def;
}

/* ── Public API ─────────────────────────────────────────────────────── */

std::string ProfileManager::profiles_dir()
{
    /* Save encoder configs in the SAME directory as dsp_effect_*.yaml and
     * mcaster1dspencoder_global.yaml — next to the .app bundle.
     * applicationDirPath() returns e.g. .../Foo.app/Contents/MacOS
     * We want the directory containing the .app bundle. */
    QString dir = QCoreApplication::applicationDirPath();
    if (dir.isEmpty())
        dir = QStringLiteral(".");

    /* Strip .app/Contents/MacOS suffix if running from a bundle */
    if (dir.contains(QStringLiteral(".app/Contents/MacOS"))) {
        int idx = dir.lastIndexOf(QStringLiteral(".app/Contents/MacOS"));
        dir = dir.left(idx);
        int slash = dir.lastIndexOf(QChar('/'));
        if (slash >= 0)
            dir = dir.left(slash);
    }

    QDir().mkpath(dir);
    fprintf(stderr, "[M11.5] ProfileManager::profiles_dir() => '%s'\n",
            dir.toUtf8().constData());
    return dir.toStdString();
}

std::string ProfileManager::profile_filename(const EncoderConfig& cfg)
{
    /* Format: {type}_encoder_{NN}.yaml
       e.g. radio_encoder_00.yaml, podcast_encoder_01.yaml, videotv_encoder_00.yaml
       If an existing config_file_path already matches, reuse its NN.
       Otherwise, auto-increment. */
    const char* tag = type_tag(cfg.encoder_type);
    std::string prefix = std::string(tag) + "_encoder_";

    /* Check if existing path is already in the current format */
    if (!cfg.config_file_path.empty()) {
        std::string fname = QFileInfo(QString::fromStdString(cfg.config_file_path))
                               .fileName().toStdString();
        if (fname.rfind(prefix, 0) == 0 && fname.size() > prefix.size() + 5 /* NN.yaml */) {
            return fname;  /* keep existing filename */
        }
    }

    int nn = next_available_number(cfg.encoder_type);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d", nn);
    return prefix + buf + ".yaml";
}

int ProfileManager::next_available_number(EncoderConfig::EncoderType type)
{
    std::string dir = profiles_dir();
    const char* tag = type_tag(type);
    std::string prefix = std::string(tag) + "_encoder_";

    QDir qdir(QString::fromStdString(dir));
    QStringList filters;
    filters << QString::fromStdString(prefix + "*.yaml");
    QStringList files = qdir.entryList(filters, QDir::Files, QDir::Name);

    int max_nn = -1;
    for (const QString& f : files) {
        std::string fname = f.toStdString();
        /* Extract NN from encoder_{type}_{NN}.yaml */
        size_t start = prefix.size();
        size_t dot = fname.rfind(".yaml");
        if (dot == std::string::npos || dot <= start) continue;
        std::string nn_str = fname.substr(start, dot - start);
        char* end = nullptr;
        long val = strtol(nn_str.c_str(), &end, 10);
        if (end != nn_str.c_str())
            max_nn = std::max(max_nn, static_cast<int>(val));
    }
    return max_nn + 1;
}

std::string ProfileManager::save_profile(EncoderConfig& cfg)
{
    std::string dir = profiles_dir();

    /* If we already have a saved path, keep using it (prevents orphan files).
       If the name/type/slot changed, delete the old file first. */
    std::string filename = profile_filename(cfg);
    std::string path = dir + "/" + filename;

    if (!cfg.config_file_path.empty() && cfg.config_file_path != path) {
        QFile::remove(QString::fromStdString(cfg.config_file_path));
    }
    if (!cfg.config_file_path.empty() &&
        QFile::exists(QString::fromStdString(cfg.config_file_path)) &&
        cfg.config_file_path == path) {
        /* Same file — overwrite in place */
    }

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "ProfileManager: cannot write" << QString::fromStdString(path);
        return {};
    }

    QTextStream ts(&file);
    ts << "# Mcaster1 Encoder Profile\n";
    ts << "# Auto-generated — do not edit while encoder is running\n\n";

    ts << "encoder:\n";
    emit_kv(ts, "name", cfg.name);
    emit_kv(ts, "type", std::string(type_tag(cfg.encoder_type)));
    emit_kv(ts, "slot_id", cfg.slot_id);
    emit_kv(ts, "codec", std::string(codec_tag(cfg.codec)));
    emit_kv(ts, "bitrate_kbps", cfg.bitrate_kbps);
    emit_kv(ts, "quality", cfg.quality);
    emit_kv(ts, "sample_rate", cfg.sample_rate);
    emit_kv(ts, "channels", cfg.channels);
    emit_kv(ts, "encode_mode", std::string(encode_mode_tag(cfg.encode_mode)));
    emit_kv(ts, "channel_mode", std::string(channel_mode_tag(cfg.channel_mode)));
    emit_kv(ts, "shuffle", cfg.shuffle);
    emit_kv(ts, "repeat_all", cfg.repeat_all);
    emit_kv(ts, "auto_start", cfg.auto_start);
    emit_kv(ts, "auto_reconnect", cfg.auto_reconnect);
    if (!cfg.playlist_path.empty())
        emit_kv(ts, "playlist_path", cfg.playlist_path);

    /* Per-encoder audio device override */
    emit_kv(ts, "per_encoder_device_index", cfg.per_encoder_device_index);

    /* Per-encoder metadata */
    emit_kv(ts, "use_global_metadata", cfg.use_global_metadata);
    if (!cfg.use_global_metadata) {
        ts << "\nper_encoder_metadata:\n";
        emit_kv(ts, "source", std::string(metadata_source_tag(cfg.per_encoder_metadata.source)));
        emit_kv(ts, "lock_metadata", cfg.per_encoder_metadata.lock_metadata);
        emit_kv(ts, "manual_text", cfg.per_encoder_metadata.manual_text);
        emit_kv(ts, "append_string", cfg.per_encoder_metadata.append_string);
        emit_kv(ts, "meta_url", cfg.per_encoder_metadata.meta_url);
        emit_kv(ts, "meta_file", cfg.per_encoder_metadata.meta_file);
        emit_kv(ts, "update_interval_sec", cfg.per_encoder_metadata.update_interval_sec);
    }

    /* DSP */
    ts << "\ndsp:\n";
    emit_kv(ts, "eq_enabled", cfg.dsp_eq_enabled);
    emit_kv(ts, "agc_enabled", cfg.dsp_agc_enabled);
    emit_kv(ts, "crossfade_enabled", cfg.dsp_crossfade_enabled);
    emit_kv(ts, "crossfade_duration", cfg.dsp_crossfade_duration);
    emit_kv(ts, "eq_preset", cfg.dsp_eq_preset);
    emit_kv(ts, "sonic_enabled", cfg.dsp_sonic_enabled);
    emit_kv(ts, "eq_mode", cfg.dsp_eq_mode);
    emit_kv(ts, "eq31_preset", cfg.dsp_eq31_preset);
    emit_kv(ts, "ptt_duck_enabled", cfg.dsp_ptt_duck_enabled);
    emit_kv(ts, "dbx_voice_enabled", cfg.dsp_dbx_voice_enabled);
    emit_kv(ts, "sonic_preset", cfg.dsp_sonic_preset);
    emit_kv(ts, "dbx_preset", cfg.dsp_dbx_preset);
    emit_kv(ts, "agc_preset", cfg.dsp_agc_preset);

    /* AGC parameters */
    ts << "\ndsp_agc:\n";
    emit_kv(ts, "input_gain_db",     cfg.agc_params.input_gain_db);
    emit_kv(ts, "gate_threshold_db", cfg.agc_params.gate_threshold_db);
    emit_kv(ts, "threshold_db",      cfg.agc_params.threshold_db);
    emit_kv(ts, "ratio",             cfg.agc_params.ratio);
    emit_kv(ts, "attack_ms",         cfg.agc_params.attack_ms);
    emit_kv(ts, "release_ms",        cfg.agc_params.release_ms);
    emit_kv(ts, "knee_db",           cfg.agc_params.knee_db);
    emit_kv(ts, "makeup_gain_db",    cfg.agc_params.makeup_gain_db);
    emit_kv(ts, "limiter_db",        cfg.agc_params.limiter_db);

    /* Sonic Enhancer parameters */
    ts << "\ndsp_sonic:\n";
    emit_kv(ts, "lo_contour",  cfg.sonic_params.lo_contour);
    emit_kv(ts, "process",     cfg.sonic_params.process);
    emit_kv(ts, "output_gain", cfg.sonic_params.output_gain);

    /* PTT Duck parameters */
    ts << "\ndsp_ptt_duck:\n";
    emit_kv(ts, "duck_level_db", cfg.ptt_duck_params.duck_level_db);
    emit_kv(ts, "attack_ms",     cfg.ptt_duck_params.attack_ms);
    emit_kv(ts, "release_ms",    cfg.ptt_duck_params.release_ms);
    emit_kv(ts, "mic_gain_db",   cfg.ptt_duck_params.mic_gain_db);

    /* DBX Voice parameters */
    ts << "\ndsp_dbx_voice:\n";
    ts << "  gate:\n";
    emit_kv4(ts, "threshold_db", cfg.dbx_voice_params.gate.threshold_db);
    emit_kv4(ts, "ratio",        cfg.dbx_voice_params.gate.ratio);
    emit_kv4(ts, "attack_ms",    cfg.dbx_voice_params.gate.attack_ms);
    emit_kv4(ts, "release_ms",   cfg.dbx_voice_params.gate.release_ms);
    emit_kv4(ts, "enabled",      cfg.dbx_voice_params.gate.enabled);
    ts << "  compressor:\n";
    emit_kv4(ts, "threshold_db", cfg.dbx_voice_params.compressor.threshold_db);
    emit_kv4(ts, "ratio",        cfg.dbx_voice_params.compressor.ratio);
    emit_kv4(ts, "attack_ms",    cfg.dbx_voice_params.compressor.attack_ms);
    emit_kv4(ts, "release_ms",   cfg.dbx_voice_params.compressor.release_ms);
    emit_kv4(ts, "enabled",      cfg.dbx_voice_params.compressor.enabled);
    ts << "  deesser:\n";
    emit_kv4(ts, "frequency_hz",  cfg.dbx_voice_params.deesser.frequency_hz);
    emit_kv4(ts, "threshold_db",  cfg.dbx_voice_params.deesser.threshold_db);
    emit_kv4(ts, "reduction_db",  cfg.dbx_voice_params.deesser.reduction_db);
    emit_kv4(ts, "bandwidth_q",   cfg.dbx_voice_params.deesser.bandwidth_q);
    emit_kv4(ts, "enabled",       cfg.dbx_voice_params.deesser.enabled);
    ts << "  lf_enhancer:\n";
    emit_kv4(ts, "frequency_hz", cfg.dbx_voice_params.lf_enhancer.frequency_hz);
    emit_kv4(ts, "amount_db",    cfg.dbx_voice_params.lf_enhancer.amount_db);
    emit_kv4(ts, "enabled",      cfg.dbx_voice_params.lf_enhancer.enabled);
    ts << "  hf_detail:\n";
    emit_kv4(ts, "frequency_hz", cfg.dbx_voice_params.hf_detail.frequency_hz);
    emit_kv4(ts, "amount_db",    cfg.dbx_voice_params.hf_detail.amount_db);
    emit_kv4(ts, "enabled",      cfg.dbx_voice_params.hf_detail.enabled);

    /* Crossfader parameters */
    ts << "\ndsp_crossfader:\n";
    emit_kv(ts, "duration_sec", cfg.crossfader_params.duration_sec);
    emit_kv(ts, "enabled",      cfg.crossfader_params.enabled);

    /* 10-band EQ parameters */
    ts << "\ndsp_eq10:\n";
    emit_kv(ts, "linked", cfg.eq10_params.linked);
    for (int i = 0; i < 10; ++i) {
        const auto& b = cfg.eq10_params.bands[i];
        ts << "  band_" << i << ":\n";
        emit_kv4(ts, "type",    eq_band_type_tag(b.type));
        emit_kv4(ts, "freq_hz", b.freq_hz);
        emit_kv4(ts, "gain_db", b.gain_db);
        emit_kv4(ts, "q",       b.q);
        emit_kv4(ts, "enabled", b.enabled);
    }

    /* 31-band EQ parameters */
    ts << "\ndsp_eq31:\n";
    emit_kv(ts, "linked", cfg.eq31_params.linked);
    for (int i = 0; i < 31; ++i) {
        char key_l[16], key_r[16];
        snprintf(key_l, sizeof(key_l), "gain_l_%d", i);
        snprintf(key_r, sizeof(key_r), "gain_r_%d", i);
        emit_kv(ts, key_l, cfg.eq31_params.gains_l[i]);
        emit_kv(ts, key_r, cfg.eq31_params.gains_r[i]);
    }

    /* Stream target */
    ts << "\nstream:\n";
    emit_kv(ts, "protocol", std::string(protocol_tag(cfg.stream_target.protocol)));
    emit_kv(ts, "host", cfg.stream_target.host);
    emit_kv(ts, "port", static_cast<int>(cfg.stream_target.port));
    emit_kv(ts, "mount", cfg.stream_target.mount);
    emit_kv(ts, "username", cfg.stream_target.username);
    emit_kv(ts, "password", cfg.stream_target.password);
    emit_kv(ts, "station_name", cfg.stream_target.station_name);
    emit_kv(ts, "description", cfg.stream_target.description);
    emit_kv(ts, "genre", cfg.stream_target.genre);
    emit_kv(ts, "url", cfg.stream_target.url);
    emit_kv(ts, "auto_reconnect", cfg.auto_reconnect);
    emit_kv(ts, "reconnect_interval_sec", cfg.reconnect_interval_sec);
    emit_kv(ts, "reconnect_max_attempts", cfg.reconnect_max_attempts);

    /* Podcast (for PODCAST type) */
    if (cfg.encoder_type == EncoderConfig::EncoderType::PODCAST) {
        ts << "\npodcast:\n";
        emit_kv(ts, "generate_rss", cfg.podcast.generate_rss);
        emit_kv(ts, "title", cfg.podcast.title);
        emit_kv(ts, "author", cfg.podcast.author);
        emit_kv(ts, "category", cfg.podcast.category);
        emit_kv(ts, "language", cfg.podcast.language);
        emit_kv(ts, "copyright", cfg.podcast.copyright);
        emit_kv(ts, "website", cfg.podcast.website);
        emit_kv(ts, "cover_art_url", cfg.podcast.cover_art_url);
        emit_kv(ts, "description", cfg.podcast.description);
        emit_kv(ts, "rss_output_dir", cfg.podcast.rss_output_dir);
        emit_kv(ts, "episode_output_dir", cfg.podcast.episode_output_dir);
        emit_kv(ts, "sftp_enabled", cfg.podcast.sftp_enabled);
        emit_kv(ts, "sftp_host", cfg.podcast.sftp_host);
        emit_kv(ts, "sftp_port", static_cast<int>(cfg.podcast.sftp_port));
        emit_kv(ts, "sftp_username", cfg.podcast.sftp_username);
        emit_kv(ts, "sftp_remote_dir", cfg.podcast.sftp_remote_dir);
        emit_kv(ts, "simulcast_dnas", cfg.podcast.simulcast_dnas);
    }

    /* Video (for TV_VIDEO type) */
    if (cfg.encoder_type == EncoderConfig::EncoderType::TV_VIDEO) {
        ts << "\nvideo:\n";
        emit_kv(ts, "enabled", cfg.video.enabled);
        emit_kv(ts, "device_index", cfg.video.device_index);
        emit_kv(ts, "width", cfg.video.width);
        emit_kv(ts, "height", cfg.video.height);
        emit_kv(ts, "fps", cfg.video.fps);
        emit_kv(ts, "bitrate_kbps", cfg.video.bitrate_kbps);
        emit_kv(ts, "codec", cfg.video.codec);
    }

    file.close();
    cfg.config_file_path = path;
    qDebug() << "ProfileManager: saved" << QString::fromStdString(path);
    return path;
}

bool ProfileManager::load_profile(const std::string& path, EncoderConfig& cfg)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) return false;

    QByteArray data = file.readAll();
    file.close();

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) return false;

    yaml_parser_set_input_string(&parser,
        reinterpret_cast<const unsigned char*>(data.constData()),
        static_cast<size_t>(data.size()));

    YamlDoc yd;
    if (!yaml_parser_load(&parser, &yd.doc)) {
        yaml_parser_delete(&parser);
        return false;
    }
    yd.valid = true;
    yaml_parser_delete(&parser);

    yaml_node_t* root = yaml_document_get_node(&yd.doc, 1);
    if (!root || root->type != YAML_MAPPING_NODE) return false;

    /* encoder section */
    yaml_node_t* enc = map_get(&yd.doc, root, "encoder");
    if (enc) {
        cfg.name = ystr(&yd.doc, enc, "name");
        cfg.encoder_type = parse_type(ystr(&yd.doc, enc, "type"));
        cfg.slot_id = yint(&yd.doc, enc, "slot_id", 0);
        cfg.codec = parse_codec(ystr(&yd.doc, enc, "codec"));
        cfg.bitrate_kbps = yint(&yd.doc, enc, "bitrate_kbps", 128);
        cfg.quality = yint(&yd.doc, enc, "quality", 5);
        cfg.sample_rate = yint(&yd.doc, enc, "sample_rate", 44100);
        cfg.channels = yint(&yd.doc, enc, "channels", 2);
        cfg.encode_mode = parse_encode_mode(ystr(&yd.doc, enc, "encode_mode"));
        cfg.channel_mode = parse_channel_mode(ystr(&yd.doc, enc, "channel_mode"));
        cfg.shuffle = ybool(&yd.doc, enc, "shuffle");
        cfg.repeat_all = ybool(&yd.doc, enc, "repeat_all", true);
        cfg.auto_start = ybool(&yd.doc, enc, "auto_start");
        cfg.auto_reconnect = ybool(&yd.doc, enc, "auto_reconnect", true);
        cfg.playlist_path = ystr(&yd.doc, enc, "playlist_path");
        cfg.per_encoder_device_index = yint(&yd.doc, enc, "per_encoder_device_index", -1);
        cfg.use_global_metadata = ybool(&yd.doc, enc, "use_global_metadata", true);
    }

    /* per_encoder_metadata section */
    yaml_node_t* pem = map_get(&yd.doc, root, "per_encoder_metadata");
    if (pem) {
        cfg.per_encoder_metadata.source = parse_metadata_source(ystr(&yd.doc, pem, "source"));
        cfg.per_encoder_metadata.lock_metadata = ybool(&yd.doc, pem, "lock_metadata");
        cfg.per_encoder_metadata.manual_text = ystr(&yd.doc, pem, "manual_text");
        cfg.per_encoder_metadata.append_string = ystr(&yd.doc, pem, "append_string");
        cfg.per_encoder_metadata.meta_url = ystr(&yd.doc, pem, "meta_url");
        cfg.per_encoder_metadata.meta_file = ystr(&yd.doc, pem, "meta_file");
        cfg.per_encoder_metadata.update_interval_sec = yint(&yd.doc, pem, "update_interval_sec", 10);
    }

    /* dsp section */
    yaml_node_t* dsp = map_get(&yd.doc, root, "dsp");
    if (dsp) {
        cfg.dsp_eq_enabled = ybool(&yd.doc, dsp, "eq_enabled");
        cfg.dsp_agc_enabled = ybool(&yd.doc, dsp, "agc_enabled");
        cfg.dsp_crossfade_enabled = ybool(&yd.doc, dsp, "crossfade_enabled", true);
        cfg.dsp_crossfade_duration = yfloat(&yd.doc, dsp, "crossfade_duration", 3.0f);
        cfg.dsp_eq_preset = ystr(&yd.doc, dsp, "eq_preset");
        cfg.dsp_sonic_enabled = ybool(&yd.doc, dsp, "sonic_enabled");
        cfg.dsp_eq_mode = yint(&yd.doc, dsp, "eq_mode", 0);
        cfg.dsp_eq31_preset = ystr(&yd.doc, dsp, "eq31_preset");
        cfg.dsp_ptt_duck_enabled = ybool(&yd.doc, dsp, "ptt_duck_enabled");
        cfg.dsp_dbx_voice_enabled = ybool(&yd.doc, dsp, "dbx_voice_enabled");
        std::string sp = ystr(&yd.doc, dsp, "sonic_preset");
        if (!sp.empty()) cfg.dsp_sonic_preset = sp;
        std::string dp = ystr(&yd.doc, dsp, "dbx_preset");
        if (!dp.empty()) cfg.dsp_dbx_preset = dp;
        std::string ap = ystr(&yd.doc, dsp, "agc_preset");
        if (!ap.empty()) cfg.dsp_agc_preset = ap;
    }

    /* AGC parameters */
    yaml_node_t* agc = map_get(&yd.doc, root, "dsp_agc");
    if (agc) {
        cfg.agc_params.input_gain_db     = yfloat(&yd.doc, agc, "input_gain_db", 0.0f);
        cfg.agc_params.gate_threshold_db = yfloat(&yd.doc, agc, "gate_threshold_db", -60.0f);
        cfg.agc_params.threshold_db      = yfloat(&yd.doc, agc, "threshold_db", -18.0f);
        cfg.agc_params.ratio             = yfloat(&yd.doc, agc, "ratio", 4.0f);
        cfg.agc_params.attack_ms         = yfloat(&yd.doc, agc, "attack_ms", 10.0f);
        cfg.agc_params.release_ms        = yfloat(&yd.doc, agc, "release_ms", 200.0f);
        cfg.agc_params.knee_db           = yfloat(&yd.doc, agc, "knee_db", 6.0f);
        cfg.agc_params.makeup_gain_db    = yfloat(&yd.doc, agc, "makeup_gain_db", 0.0f);
        cfg.agc_params.limiter_db        = yfloat(&yd.doc, agc, "limiter_db", -1.0f);
    }

    /* Sonic Enhancer parameters */
    yaml_node_t* sonic = map_get(&yd.doc, root, "dsp_sonic");
    if (sonic) {
        cfg.sonic_params.lo_contour  = yfloat(&yd.doc, sonic, "lo_contour", 5.0f);
        cfg.sonic_params.process     = yfloat(&yd.doc, sonic, "process", 5.0f);
        cfg.sonic_params.output_gain = yfloat(&yd.doc, sonic, "output_gain", 0.0f);
    }

    /* PTT Duck parameters */
    yaml_node_t* ptt = map_get(&yd.doc, root, "dsp_ptt_duck");
    if (ptt) {
        cfg.ptt_duck_params.duck_level_db = yfloat(&yd.doc, ptt, "duck_level_db", -12.0f);
        cfg.ptt_duck_params.attack_ms     = yfloat(&yd.doc, ptt, "attack_ms", 5.0f);
        cfg.ptt_duck_params.release_ms    = yfloat(&yd.doc, ptt, "release_ms", 100.0f);
        cfg.ptt_duck_params.mic_gain_db   = yfloat(&yd.doc, ptt, "mic_gain_db", 0.0f);
    }

    /* DBX Voice parameters */
    yaml_node_t* dbx = map_get(&yd.doc, root, "dsp_dbx_voice");
    if (dbx) {
        yaml_node_t* gate = map_get(&yd.doc, dbx, "gate");
        if (gate) {
            cfg.dbx_voice_params.gate.threshold_db = yfloat(&yd.doc, gate, "threshold_db", -50.0f);
            cfg.dbx_voice_params.gate.ratio        = yfloat(&yd.doc, gate, "ratio", 2.0f);
            cfg.dbx_voice_params.gate.attack_ms    = yfloat(&yd.doc, gate, "attack_ms", 1.0f);
            cfg.dbx_voice_params.gate.release_ms   = yfloat(&yd.doc, gate, "release_ms", 100.0f);
            cfg.dbx_voice_params.gate.enabled       = ybool(&yd.doc, gate, "enabled", true);
        }
        yaml_node_t* comp = map_get(&yd.doc, dbx, "compressor");
        if (comp) {
            cfg.dbx_voice_params.compressor.threshold_db = yfloat(&yd.doc, comp, "threshold_db", -20.0f);
            cfg.dbx_voice_params.compressor.ratio        = yfloat(&yd.doc, comp, "ratio", 3.0f);
            cfg.dbx_voice_params.compressor.attack_ms    = yfloat(&yd.doc, comp, "attack_ms", 5.0f);
            cfg.dbx_voice_params.compressor.release_ms   = yfloat(&yd.doc, comp, "release_ms", 150.0f);
            cfg.dbx_voice_params.compressor.enabled       = ybool(&yd.doc, comp, "enabled", true);
        }
        yaml_node_t* deess = map_get(&yd.doc, dbx, "deesser");
        if (deess) {
            cfg.dbx_voice_params.deesser.frequency_hz = yfloat(&yd.doc, deess, "frequency_hz", 6000.0f);
            cfg.dbx_voice_params.deesser.threshold_db = yfloat(&yd.doc, deess, "threshold_db", -20.0f);
            cfg.dbx_voice_params.deesser.reduction_db = yfloat(&yd.doc, deess, "reduction_db", -6.0f);
            cfg.dbx_voice_params.deesser.bandwidth_q  = yfloat(&yd.doc, deess, "bandwidth_q", 2.0f);
            cfg.dbx_voice_params.deesser.enabled       = ybool(&yd.doc, deess, "enabled", true);
        }
        yaml_node_t* lf = map_get(&yd.doc, dbx, "lf_enhancer");
        if (lf) {
            cfg.dbx_voice_params.lf_enhancer.frequency_hz = yfloat(&yd.doc, lf, "frequency_hz", 120.0f);
            cfg.dbx_voice_params.lf_enhancer.amount_db    = yfloat(&yd.doc, lf, "amount_db", 3.0f);
            cfg.dbx_voice_params.lf_enhancer.enabled       = ybool(&yd.doc, lf, "enabled", true);
        }
        yaml_node_t* hf = map_get(&yd.doc, dbx, "hf_detail");
        if (hf) {
            cfg.dbx_voice_params.hf_detail.frequency_hz = yfloat(&yd.doc, hf, "frequency_hz", 4000.0f);
            cfg.dbx_voice_params.hf_detail.amount_db    = yfloat(&yd.doc, hf, "amount_db", 3.0f);
            cfg.dbx_voice_params.hf_detail.enabled       = ybool(&yd.doc, hf, "enabled", true);
        }
    }

    /* Crossfader parameters */
    yaml_node_t* xfade = map_get(&yd.doc, root, "dsp_crossfader");
    if (xfade) {
        cfg.crossfader_params.duration_sec = yfloat(&yd.doc, xfade, "duration_sec", 3.0f);
        cfg.crossfader_params.enabled      = ybool(&yd.doc, xfade, "enabled", true);
    }

    /* 10-band EQ parameters */
    yaml_node_t* eq10 = map_get(&yd.doc, root, "dsp_eq10");
    if (eq10) {
        cfg.eq10_params.linked = ybool(&yd.doc, eq10, "linked", true);
        for (int i = 0; i < 10; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "band_%d", i);
            yaml_node_t* band = map_get(&yd.doc, eq10, key);
            if (band) {
                std::string type_str = ystr(&yd.doc, band, "type");
                if (!type_str.empty())
                    cfg.eq10_params.bands[i].type = parse_eq_band_type(type_str);
                cfg.eq10_params.bands[i].freq_hz = yfloat(&yd.doc, band, "freq_hz", 1000.0f);
                cfg.eq10_params.bands[i].gain_db = yfloat(&yd.doc, band, "gain_db", 0.0f);
                cfg.eq10_params.bands[i].q       = yfloat(&yd.doc, band, "q", 1.0f);
                cfg.eq10_params.bands[i].enabled  = ybool(&yd.doc, band, "enabled", true);
            }
        }
    }

    /* 31-band EQ parameters */
    yaml_node_t* eq31 = map_get(&yd.doc, root, "dsp_eq31");
    if (eq31) {
        cfg.eq31_params.linked = ybool(&yd.doc, eq31, "linked", true);
        for (int i = 0; i < 31; ++i) {
            char key_l[16], key_r[16];
            snprintf(key_l, sizeof(key_l), "gain_l_%d", i);
            snprintf(key_r, sizeof(key_r), "gain_r_%d", i);
            cfg.eq31_params.gains_l[i] = yfloat(&yd.doc, eq31, key_l, 0.0f);
            cfg.eq31_params.gains_r[i] = yfloat(&yd.doc, eq31, key_r, 0.0f);
        }
    }

    /* stream section */
    yaml_node_t* st = map_get(&yd.doc, root, "stream");
    if (st) {
        cfg.stream_target.protocol = parse_protocol(ystr(&yd.doc, st, "protocol"));
        cfg.stream_target.host = ystr(&yd.doc, st, "host");
        cfg.stream_target.port = static_cast<uint16_t>(yint(&yd.doc, st, "port", 8000));
        cfg.stream_target.mount = ystr(&yd.doc, st, "mount");
        cfg.stream_target.username = ystr(&yd.doc, st, "username");
        cfg.stream_target.password = ystr(&yd.doc, st, "password");
        cfg.stream_target.station_name = ystr(&yd.doc, st, "station_name");
        cfg.stream_target.description = ystr(&yd.doc, st, "description");
        cfg.stream_target.genre = ystr(&yd.doc, st, "genre");
        cfg.stream_target.url = ystr(&yd.doc, st, "url");
        cfg.auto_reconnect = ybool(&yd.doc, st, "auto_reconnect", true);
        cfg.reconnect_interval_sec = yint(&yd.doc, st, "reconnect_interval_sec", 5);
        cfg.reconnect_max_attempts = yint(&yd.doc, st, "reconnect_max_attempts", 0);
    }

    /* podcast section */
    yaml_node_t* pod = map_get(&yd.doc, root, "podcast");
    if (pod) {
        cfg.podcast.generate_rss = ybool(&yd.doc, pod, "generate_rss");
        cfg.podcast.title = ystr(&yd.doc, pod, "title");
        cfg.podcast.author = ystr(&yd.doc, pod, "author");
        cfg.podcast.category = ystr(&yd.doc, pod, "category");
        cfg.podcast.language = ystr(&yd.doc, pod, "language");
        cfg.podcast.copyright = ystr(&yd.doc, pod, "copyright");
        cfg.podcast.website = ystr(&yd.doc, pod, "website");
        cfg.podcast.cover_art_url = ystr(&yd.doc, pod, "cover_art_url");
        cfg.podcast.description = ystr(&yd.doc, pod, "description");
        cfg.podcast.rss_output_dir = ystr(&yd.doc, pod, "rss_output_dir");
        cfg.podcast.episode_output_dir = ystr(&yd.doc, pod, "episode_output_dir");
        cfg.podcast.sftp_enabled = ybool(&yd.doc, pod, "sftp_enabled");
        cfg.podcast.sftp_host = ystr(&yd.doc, pod, "sftp_host");
        cfg.podcast.sftp_port = static_cast<uint16_t>(yint(&yd.doc, pod, "sftp_port", 22));
        cfg.podcast.sftp_username = ystr(&yd.doc, pod, "sftp_username");
        cfg.podcast.sftp_remote_dir = ystr(&yd.doc, pod, "sftp_remote_dir");
        cfg.podcast.simulcast_dnas = ybool(&yd.doc, pod, "simulcast_dnas");
    }

    /* video section */
    yaml_node_t* vid = map_get(&yd.doc, root, "video");
    if (vid) {
        cfg.video.enabled = ybool(&yd.doc, vid, "enabled");
        cfg.video.device_index = yint(&yd.doc, vid, "device_index", 0);
        cfg.video.width = yint(&yd.doc, vid, "width", 1280);
        cfg.video.height = yint(&yd.doc, vid, "height", 720);
        cfg.video.fps = yint(&yd.doc, vid, "fps", 30);
        cfg.video.bitrate_kbps = yint(&yd.doc, vid, "bitrate_kbps", 2500);
        cfg.video.codec = ystr(&yd.doc, vid, "codec");
    }

    cfg.config_file_path = path;
    return true;
}

std::vector<EncoderConfig> ProfileManager::scan_profiles()
{
    std::vector<EncoderConfig> result;
    std::string dir = profiles_dir();

    /* Migrate old-format files from profiles/ subdir if they exist */
    migrate_old_profiles(dir);

    QDir qdir(QString::fromStdString(dir));
    QStringList filters;
    /* Only match encoder profile files: {type}_encoder_NN.yaml */
    filters << QStringLiteral("radio_encoder_*.yaml")
            << QStringLiteral("podcast_encoder_*.yaml")
            << QStringLiteral("videotv_encoder_*.yaml");
    QStringList files = qdir.entryList(filters, QDir::Files, QDir::Name);

    fprintf(stderr, "[M11.5] scan_profiles: scanning '%s' — found %d encoder profile(s)\n",
            dir.c_str(), static_cast<int>(files.size()));

    for (const QString& f : files) {
        std::string path = dir + "/" + f.toStdString();
        EncoderConfig cfg;
        if (load_profile(path, cfg)) {
            fprintf(stderr, "[M11.5]   profile: '%s' type=%d name='%s' agc_en=%d sonic_en=%d\n",
                    f.toUtf8().constData(), static_cast<int>(cfg.encoder_type),
                    cfg.name.c_str(), cfg.dsp_agc_enabled, cfg.dsp_sonic_enabled);
            result.push_back(std::move(cfg));
        } else {
            fprintf(stderr, "[M11.5]   FAILED to parse: '%s'\n", f.toUtf8().constData());
        }
    }

    return result;
}

bool ProfileManager::delete_profile(const std::string& path)
{
    return QFile::remove(QString::fromStdString(path));
}

} // namespace mc1
