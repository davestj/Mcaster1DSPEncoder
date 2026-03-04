/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * global_config_manager.cpp — YAML persistence for global settings & DSP rack defaults
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "global_config_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

#include <yaml.h>
#include <cstring>

namespace mc1 {

/* ── YAML reading helpers (same pattern as profile_manager.cpp) ─────── */

struct YamlDoc {
    yaml_document_t doc;
    bool valid = false;
    ~YamlDoc() { if (valid) yaml_document_delete(&doc); }
};

static const char* node_scalar(yaml_document_t*, yaml_node_t* node)
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

static yaml_node_t* parse_yaml_file(const std::string& path, YamlDoc& yd)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) return nullptr;

    QByteArray data = file.readAll();
    file.close();

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) return nullptr;

    yaml_parser_set_input_string(&parser,
        reinterpret_cast<const unsigned char*>(data.constData()),
        static_cast<size_t>(data.size()));

    if (!yaml_parser_load(&parser, &yd.doc)) {
        yaml_parser_delete(&parser);
        return nullptr;
    }
    yd.valid = true;
    yaml_parser_delete(&parser);

    yaml_node_t* root = yaml_document_get_node(&yd.doc, 1);
    if (!root || root->type != YAML_MAPPING_NODE) return nullptr;
    return root;
}

/* ── YAML writing helpers ──────────────────────────────────────────── */

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

static void emit_kv4(QTextStream& ts, const char* key, float val)
{
    ts << "    " << key << ": " << val << "\n";
}

static void emit_kv4(QTextStream& ts, const char* key, bool val)
{
    ts << "    " << key << ": " << (val ? "true" : "false") << "\n";
}

static void emit_kv4(QTextStream& ts, const char* key, const std::string& val)
{
    ts << "    " << key << ": \"" << QString::fromStdString(val) << "\"\n";
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

/* ── Public API ─────────────────────────────────────────────────────── */

std::string GlobalConfigManager::config_dir()
{
    /* Save configs next to the .app bundle (not inside it).
     * applicationDirPath() returns e.g. .../Foo.app/Contents/MacOS
     * We want the directory containing the .app bundle. */
    QString dir = QCoreApplication::applicationDirPath();
    if (dir.isEmpty())
        dir = QStringLiteral(".");

    /* Strip .app/Contents/MacOS suffix if running from a bundle */
    if (dir.contains(QStringLiteral(".app/Contents/MacOS"))) {
        int idx = dir.lastIndexOf(QStringLiteral(".app/Contents/MacOS"));
        dir = dir.left(idx);                   /* .../src/macos/Foo */
        int slash = dir.lastIndexOf(QChar('/'));
        if (slash >= 0)
            dir = dir.left(slash);             /* .../src/macos */
    }

    QDir().mkpath(dir);
    fprintf(stderr, "[M11.5] GlobalConfigManager::config_dir() => '%s'\n",
            dir.toUtf8().constData());
    return dir.toStdString();
}

/* ── Global Config ──────────────────────────────────────────────────── */

GlobalConfig GlobalConfigManager::load_global()
{
    GlobalConfig cfg;
    std::string path = config_dir() + "/" + kGlobalFilename;

    YamlDoc yd;
    yaml_node_t* root = parse_yaml_file(path, yd);
    if (!root) return cfg;

    yaml_node_t* g = map_get(&yd.doc, root, "global");
    if (!g) g = root;  /* top-level fallback */

    cfg.audio_device_index = yint(&yd.doc, g, "audio_device_index", -1);
    cfg.audio_device_uid   = ystr(&yd.doc, g, "audio_device_uid");
    cfg.video_device_index = yint(&yd.doc, g, "video_device_index", 0);
    cfg.window_x           = yint(&yd.doc, g, "window_x", -1);
    cfg.window_y           = yint(&yd.doc, g, "window_y", -1);
    cfg.window_width       = yint(&yd.doc, g, "window_width", 900);
    cfg.window_height      = yint(&yd.doc, g, "window_height", 620);
    cfg.window_maximized   = ybool(&yd.doc, g, "window_maximized");
    cfg.theme              = yint(&yd.doc, g, "theme", 0);
    cfg.log_level          = yint(&yd.doc, g, "log_level", 4);
    cfg.log_dir            = ystr(&yd.doc, g, "log_dir");
    cfg.dnas_poll_sec      = yint(&yd.doc, g, "dnas_poll_sec", 15);
    cfg.auto_start         = ybool(&yd.doc, g, "auto_start");
    cfg.tray_on_close      = ybool(&yd.doc, g, "tray_on_close", true);
    cfg.playlist_dir       = ystr(&yd.doc, g, "playlist_dir");
    cfg.archive_dir        = ystr(&yd.doc, g, "archive_dir");

    qDebug() << "GlobalConfigManager: loaded" << QString::fromStdString(path);
    return cfg;
}

std::string GlobalConfigManager::save_global(const GlobalConfig& cfg)
{
    std::string dir = config_dir();
    std::string path = dir + "/" + kGlobalFilename;

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "GlobalConfigManager: cannot write" << QString::fromStdString(path);
        return {};
    }

    QTextStream ts(&file);
    ts << "# Mcaster1 DSP Encoder — Global Settings\n";
    ts << "# Auto-generated — do not edit while encoder is running\n\n";

    ts << "global:\n";
    emit_kv(ts, "audio_device_index", cfg.audio_device_index);
    emit_kv(ts, "audio_device_uid",   cfg.audio_device_uid);
    emit_kv(ts, "video_device_index", cfg.video_device_index);
    emit_kv(ts, "window_x",           cfg.window_x);
    emit_kv(ts, "window_y",           cfg.window_y);
    emit_kv(ts, "window_width",       cfg.window_width);
    emit_kv(ts, "window_height",      cfg.window_height);
    emit_kv(ts, "window_maximized",   cfg.window_maximized);
    emit_kv(ts, "theme",              cfg.theme);
    emit_kv(ts, "log_level",          cfg.log_level);
    emit_kv(ts, "log_dir",            cfg.log_dir);
    emit_kv(ts, "dnas_poll_sec",      cfg.dnas_poll_sec);
    emit_kv(ts, "auto_start",         cfg.auto_start);
    emit_kv(ts, "tray_on_close",      cfg.tray_on_close);
    emit_kv(ts, "playlist_dir",       cfg.playlist_dir);
    emit_kv(ts, "archive_dir",        cfg.archive_dir);

    file.close();
    qDebug() << "GlobalConfigManager: saved" << QString::fromStdString(path);
    return path;
}

/* ── Per-unit YAML filenames ──────────────────────────────────────── */

static constexpr const char* kUnitFilenames[] = {
    "dsp_effect_agc.yaml",
    "dsp_effect_sonic_enhancer.yaml",
    "dsp_effect_eq10.yaml",
    "dsp_effect_eq31.yaml",
    "dsp_effect_ptt_duck.yaml",
    "dsp_effect_dbx_voice.yaml"
};

const char* GlobalConfigManager::unit_filename(DspUnitId unit)
{
    int idx = static_cast<int>(unit);
    if (idx < 0 || idx >= static_cast<int>(DspUnitId::COUNT))
        return "dsp_effect_unknown.yaml";
    return kUnitFilenames[idx];
}

/* ── DSP Rack Defaults ──────────────────────────────────────────────── */

/* Load a single per-unit YAML file and populate the relevant section of dsp */
static void load_unit_from_file(const std::string& path, DspUnitId unit, DspRackDefaults& dsp)
{
    YamlDoc yd;
    yaml_node_t* root = parse_yaml_file(path, yd);
    if (!root) return;

    switch (unit) {
    case DspUnitId::AGC: {
        yaml_node_t* agc = map_get(&yd.doc, root, "agc");
        if (!agc) agc = root;
        dsp.agc.input_gain_db     = yfloat(&yd.doc, agc, "input_gain_db", 0.0f);
        dsp.agc.gate_threshold_db = yfloat(&yd.doc, agc, "gate_threshold_db", -60.0f);
        dsp.agc.threshold_db      = yfloat(&yd.doc, agc, "threshold_db", -18.0f);
        dsp.agc.ratio             = yfloat(&yd.doc, agc, "ratio", 4.0f);
        dsp.agc.attack_ms         = yfloat(&yd.doc, agc, "attack_ms", 10.0f);
        dsp.agc.release_ms        = yfloat(&yd.doc, agc, "release_ms", 200.0f);
        dsp.agc.knee_db           = yfloat(&yd.doc, agc, "knee_db", 6.0f);
        dsp.agc.makeup_gain_db    = yfloat(&yd.doc, agc, "makeup_gain_db", 0.0f);
        dsp.agc.limiter_db        = yfloat(&yd.doc, agc, "limiter_db", -1.0f);
        std::string p = ystr(&yd.doc, root, "preset");
        if (!p.empty()) dsp.agc_preset = p;
        break;
    }
    case DspUnitId::SONIC_ENHANCER: {
        yaml_node_t* sonic = map_get(&yd.doc, root, "sonic");
        if (!sonic) sonic = root;
        dsp.sonic.lo_contour  = yfloat(&yd.doc, sonic, "lo_contour", 5.0f);
        dsp.sonic.process     = yfloat(&yd.doc, sonic, "process", 5.0f);
        dsp.sonic.output_gain = yfloat(&yd.doc, sonic, "output_gain", 0.0f);
        std::string p = ystr(&yd.doc, root, "preset");
        if (!p.empty()) dsp.sonic_preset = p;
        break;
    }
    case DspUnitId::EQ10: {
        yaml_node_t* eq10 = map_get(&yd.doc, root, "eq10");
        if (!eq10) eq10 = root;
        dsp.eq10.linked = ybool(&yd.doc, eq10, "linked", true);
        for (int i = 0; i < 10; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "band_%d", i);
            yaml_node_t* band = map_get(&yd.doc, eq10, key);
            if (band) {
                std::string type_str = ystr(&yd.doc, band, "type");
                if (!type_str.empty())
                    dsp.eq10.bands[i].type = parse_eq_band_type(type_str);
                dsp.eq10.bands[i].freq_hz = yfloat(&yd.doc, band, "freq_hz", 1000.0f);
                dsp.eq10.bands[i].gain_db = yfloat(&yd.doc, band, "gain_db", 0.0f);
                dsp.eq10.bands[i].q       = yfloat(&yd.doc, band, "q", 1.0f);
                dsp.eq10.bands[i].enabled = ybool(&yd.doc, band, "enabled", true);
            }
        }
        std::string p = ystr(&yd.doc, root, "preset");
        if (!p.empty()) dsp.eq10_preset = p;
        break;
    }
    case DspUnitId::EQ31: {
        yaml_node_t* eq31 = map_get(&yd.doc, root, "eq31");
        if (!eq31) eq31 = root;
        dsp.eq31.linked = ybool(&yd.doc, eq31, "linked", true);
        for (int i = 0; i < 31; ++i) {
            char key_l[16], key_r[16];
            snprintf(key_l, sizeof(key_l), "gain_l_%d", i);
            snprintf(key_r, sizeof(key_r), "gain_r_%d", i);
            dsp.eq31.gains_l[i] = yfloat(&yd.doc, eq31, key_l, 0.0f);
            dsp.eq31.gains_r[i] = yfloat(&yd.doc, eq31, key_r, 0.0f);
        }
        std::string p = ystr(&yd.doc, root, "preset");
        if (!p.empty()) dsp.eq31_preset = p;
        break;
    }
    case DspUnitId::PTT_DUCK: {
        yaml_node_t* ptt = map_get(&yd.doc, root, "ptt_duck");
        if (!ptt) ptt = root;
        dsp.ptt_duck.duck_level_db = yfloat(&yd.doc, ptt, "duck_level_db", -12.0f);
        dsp.ptt_duck.attack_ms     = yfloat(&yd.doc, ptt, "attack_ms", 5.0f);
        dsp.ptt_duck.release_ms    = yfloat(&yd.doc, ptt, "release_ms", 100.0f);
        dsp.ptt_duck.mic_gain_db   = yfloat(&yd.doc, ptt, "mic_gain_db", 0.0f);
        break;
    }
    case DspUnitId::DBX_VOICE: {
        yaml_node_t* dbx = map_get(&yd.doc, root, "dbx_voice");
        if (!dbx) dbx = root;
        yaml_node_t* gate = map_get(&yd.doc, dbx, "gate");
        if (gate) {
            dsp.dbx_voice.gate.threshold_db = yfloat(&yd.doc, gate, "threshold_db", -50.0f);
            dsp.dbx_voice.gate.ratio        = yfloat(&yd.doc, gate, "ratio", 2.0f);
            dsp.dbx_voice.gate.attack_ms    = yfloat(&yd.doc, gate, "attack_ms", 1.0f);
            dsp.dbx_voice.gate.release_ms   = yfloat(&yd.doc, gate, "release_ms", 100.0f);
            dsp.dbx_voice.gate.enabled      = ybool(&yd.doc, gate, "enabled", true);
        }
        yaml_node_t* comp = map_get(&yd.doc, dbx, "compressor");
        if (comp) {
            dsp.dbx_voice.compressor.threshold_db = yfloat(&yd.doc, comp, "threshold_db", -20.0f);
            dsp.dbx_voice.compressor.ratio        = yfloat(&yd.doc, comp, "ratio", 3.0f);
            dsp.dbx_voice.compressor.attack_ms    = yfloat(&yd.doc, comp, "attack_ms", 5.0f);
            dsp.dbx_voice.compressor.release_ms   = yfloat(&yd.doc, comp, "release_ms", 150.0f);
            dsp.dbx_voice.compressor.enabled      = ybool(&yd.doc, comp, "enabled", true);
        }
        yaml_node_t* deess = map_get(&yd.doc, dbx, "deesser");
        if (deess) {
            dsp.dbx_voice.deesser.frequency_hz = yfloat(&yd.doc, deess, "frequency_hz", 6000.0f);
            dsp.dbx_voice.deesser.threshold_db = yfloat(&yd.doc, deess, "threshold_db", -20.0f);
            dsp.dbx_voice.deesser.reduction_db = yfloat(&yd.doc, deess, "reduction_db", -6.0f);
            dsp.dbx_voice.deesser.bandwidth_q  = yfloat(&yd.doc, deess, "bandwidth_q", 2.0f);
            dsp.dbx_voice.deesser.enabled      = ybool(&yd.doc, deess, "enabled", true);
        }
        yaml_node_t* lf = map_get(&yd.doc, dbx, "lf_enhancer");
        if (lf) {
            dsp.dbx_voice.lf_enhancer.frequency_hz = yfloat(&yd.doc, lf, "frequency_hz", 120.0f);
            dsp.dbx_voice.lf_enhancer.amount_db    = yfloat(&yd.doc, lf, "amount_db", 3.0f);
            dsp.dbx_voice.lf_enhancer.enabled      = ybool(&yd.doc, lf, "enabled", true);
        }
        yaml_node_t* hf = map_get(&yd.doc, dbx, "hf_detail");
        if (hf) {
            dsp.dbx_voice.hf_detail.frequency_hz = yfloat(&yd.doc, hf, "frequency_hz", 4000.0f);
            dsp.dbx_voice.hf_detail.amount_db    = yfloat(&yd.doc, hf, "amount_db", 3.0f);
            dsp.dbx_voice.hf_detail.enabled      = ybool(&yd.doc, hf, "enabled", true);
        }
        std::string p = ystr(&yd.doc, root, "preset");
        if (!p.empty()) dsp.dbx_preset = p;
        break;
    }
    default: break;
    }
}

DspRackDefaults GlobalConfigManager::load_all_dsp_units()
{
    DspRackDefaults dsp;
    std::string dir = config_dir();
    bool any_found = false;

    for (int i = 0; i < static_cast<int>(DspUnitId::COUNT); ++i) {
        std::string path = dir + "/" + kUnitFilenames[i];
        QFile f(QString::fromStdString(path));
        if (f.exists()) {
            load_unit_from_file(path, static_cast<DspUnitId>(i), dsp);
            any_found = true;
        }
    }

    if (any_found) {
        qDebug() << "GlobalConfigManager: loaded per-unit DSP files";
    }
    return dsp;
}

DspRackDefaults GlobalConfigManager::load_dsp_defaults()
{
    /* Try per-unit files first */
    std::string dir = config_dir();
    bool has_per_unit = false;
    for (int i = 0; i < static_cast<int>(DspUnitId::COUNT); ++i) {
        QFile f(QString::fromStdString(dir + "/" + kUnitFilenames[i]));
        if (f.exists()) { has_per_unit = true; break; }
    }

    if (has_per_unit) {
        fprintf(stderr, "[M11.5] load_dsp_defaults: loading from per-unit YAML files in '%s'\n",
                dir.c_str());
        return load_all_dsp_units();
    }

    fprintf(stderr, "[M11.5] load_dsp_defaults: no per-unit files found, using defaults\n");
    /* Fall back to legacy monolithic file */
    DspRackDefaults dsp;
    std::string path = config_dir() + "/" + kDspFilename;

    YamlDoc yd;
    yaml_node_t* root = parse_yaml_file(path, yd);
    if (!root) return dsp;

    /* AGC */
    yaml_node_t* agc = map_get(&yd.doc, root, "agc");
    if (agc) {
        dsp.agc.input_gain_db     = yfloat(&yd.doc, agc, "input_gain_db", 0.0f);
        dsp.agc.gate_threshold_db = yfloat(&yd.doc, agc, "gate_threshold_db", -60.0f);
        dsp.agc.threshold_db      = yfloat(&yd.doc, agc, "threshold_db", -18.0f);
        dsp.agc.ratio             = yfloat(&yd.doc, agc, "ratio", 4.0f);
        dsp.agc.attack_ms         = yfloat(&yd.doc, agc, "attack_ms", 10.0f);
        dsp.agc.release_ms        = yfloat(&yd.doc, agc, "release_ms", 200.0f);
        dsp.agc.knee_db           = yfloat(&yd.doc, agc, "knee_db", 6.0f);
        dsp.agc.makeup_gain_db    = yfloat(&yd.doc, agc, "makeup_gain_db", 0.0f);
        dsp.agc.limiter_db        = yfloat(&yd.doc, agc, "limiter_db", -1.0f);
    }

    /* Sonic Enhancer */
    yaml_node_t* sonic = map_get(&yd.doc, root, "sonic");
    if (sonic) {
        dsp.sonic.lo_contour  = yfloat(&yd.doc, sonic, "lo_contour", 5.0f);
        dsp.sonic.process     = yfloat(&yd.doc, sonic, "process", 5.0f);
        dsp.sonic.output_gain = yfloat(&yd.doc, sonic, "output_gain", 0.0f);
    }

    /* PTT Duck */
    yaml_node_t* ptt = map_get(&yd.doc, root, "ptt_duck");
    if (ptt) {
        dsp.ptt_duck.duck_level_db = yfloat(&yd.doc, ptt, "duck_level_db", -12.0f);
        dsp.ptt_duck.attack_ms     = yfloat(&yd.doc, ptt, "attack_ms", 5.0f);
        dsp.ptt_duck.release_ms    = yfloat(&yd.doc, ptt, "release_ms", 100.0f);
        dsp.ptt_duck.mic_gain_db   = yfloat(&yd.doc, ptt, "mic_gain_db", 0.0f);
    }

    /* DBX Voice */
    yaml_node_t* dbx = map_get(&yd.doc, root, "dbx_voice");
    if (dbx) {
        yaml_node_t* gate = map_get(&yd.doc, dbx, "gate");
        if (gate) {
            dsp.dbx_voice.gate.threshold_db = yfloat(&yd.doc, gate, "threshold_db", -50.0f);
            dsp.dbx_voice.gate.ratio        = yfloat(&yd.doc, gate, "ratio", 2.0f);
            dsp.dbx_voice.gate.attack_ms    = yfloat(&yd.doc, gate, "attack_ms", 1.0f);
            dsp.dbx_voice.gate.release_ms   = yfloat(&yd.doc, gate, "release_ms", 100.0f);
            dsp.dbx_voice.gate.enabled      = ybool(&yd.doc, gate, "enabled", true);
        }
        yaml_node_t* comp = map_get(&yd.doc, dbx, "compressor");
        if (comp) {
            dsp.dbx_voice.compressor.threshold_db = yfloat(&yd.doc, comp, "threshold_db", -20.0f);
            dsp.dbx_voice.compressor.ratio        = yfloat(&yd.doc, comp, "ratio", 3.0f);
            dsp.dbx_voice.compressor.attack_ms    = yfloat(&yd.doc, comp, "attack_ms", 5.0f);
            dsp.dbx_voice.compressor.release_ms   = yfloat(&yd.doc, comp, "release_ms", 150.0f);
            dsp.dbx_voice.compressor.enabled      = ybool(&yd.doc, comp, "enabled", true);
        }
        yaml_node_t* deess = map_get(&yd.doc, dbx, "deesser");
        if (deess) {
            dsp.dbx_voice.deesser.frequency_hz = yfloat(&yd.doc, deess, "frequency_hz", 6000.0f);
            dsp.dbx_voice.deesser.threshold_db = yfloat(&yd.doc, deess, "threshold_db", -20.0f);
            dsp.dbx_voice.deesser.reduction_db = yfloat(&yd.doc, deess, "reduction_db", -6.0f);
            dsp.dbx_voice.deesser.bandwidth_q  = yfloat(&yd.doc, deess, "bandwidth_q", 2.0f);
            dsp.dbx_voice.deesser.enabled      = ybool(&yd.doc, deess, "enabled", true);
        }
        yaml_node_t* lf = map_get(&yd.doc, dbx, "lf_enhancer");
        if (lf) {
            dsp.dbx_voice.lf_enhancer.frequency_hz = yfloat(&yd.doc, lf, "frequency_hz", 120.0f);
            dsp.dbx_voice.lf_enhancer.amount_db    = yfloat(&yd.doc, lf, "amount_db", 3.0f);
            dsp.dbx_voice.lf_enhancer.enabled      = ybool(&yd.doc, lf, "enabled", true);
        }
        yaml_node_t* hf = map_get(&yd.doc, dbx, "hf_detail");
        if (hf) {
            dsp.dbx_voice.hf_detail.frequency_hz = yfloat(&yd.doc, hf, "frequency_hz", 4000.0f);
            dsp.dbx_voice.hf_detail.amount_db    = yfloat(&yd.doc, hf, "amount_db", 3.0f);
            dsp.dbx_voice.hf_detail.enabled      = ybool(&yd.doc, hf, "enabled", true);
        }
    }

    /* Crossfader */
    yaml_node_t* xfade = map_get(&yd.doc, root, "crossfader");
    if (xfade) {
        dsp.crossfader.duration_sec = yfloat(&yd.doc, xfade, "duration_sec", 3.0f);
        dsp.crossfader.enabled      = ybool(&yd.doc, xfade, "enabled", true);
    }

    /* 10-band EQ */
    yaml_node_t* eq10 = map_get(&yd.doc, root, "eq10");
    if (eq10) {
        dsp.eq10.linked = ybool(&yd.doc, eq10, "linked", true);
        for (int i = 0; i < 10; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "band_%d", i);
            yaml_node_t* band = map_get(&yd.doc, eq10, key);
            if (band) {
                std::string type_str = ystr(&yd.doc, band, "type");
                if (!type_str.empty())
                    dsp.eq10.bands[i].type = parse_eq_band_type(type_str);
                dsp.eq10.bands[i].freq_hz = yfloat(&yd.doc, band, "freq_hz", 1000.0f);
                dsp.eq10.bands[i].gain_db = yfloat(&yd.doc, band, "gain_db", 0.0f);
                dsp.eq10.bands[i].q       = yfloat(&yd.doc, band, "q", 1.0f);
                dsp.eq10.bands[i].enabled = ybool(&yd.doc, band, "enabled", true);
            }
        }
    }

    /* 31-band EQ */
    yaml_node_t* eq31 = map_get(&yd.doc, root, "eq31");
    if (eq31) {
        dsp.eq31.linked = ybool(&yd.doc, eq31, "linked", true);
        for (int i = 0; i < 31; ++i) {
            char key_l[16], key_r[16];
            snprintf(key_l, sizeof(key_l), "gain_l_%d", i);
            snprintf(key_r, sizeof(key_r), "gain_r_%d", i);
            dsp.eq31.gains_l[i] = yfloat(&yd.doc, eq31, key_l, 0.0f);
            dsp.eq31.gains_r[i] = yfloat(&yd.doc, eq31, key_r, 0.0f);
        }
    }

    qDebug() << "GlobalConfigManager: loaded DSP defaults from" << QString::fromStdString(path);
    return dsp;
}

std::string GlobalConfigManager::save_dsp_defaults(const DspRackDefaults& dsp)
{
    /* Save each unit to its own YAML file */
    for (int i = 0; i < static_cast<int>(DspUnitId::COUNT); ++i)
        save_dsp_unit(static_cast<DspUnitId>(i), dsp);
    return config_dir();
}

/* ── Per-Unit Save ──────────────────────────────────────────────────── */

bool GlobalConfigManager::save_dsp_unit(DspUnitId unit, const DspRackDefaults& dsp)
{
    std::string dir = config_dir();
    std::string path = dir + "/" + unit_filename(unit);

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "GlobalConfigManager: cannot write" << QString::fromStdString(path);
        return false;
    }

    QTextStream ts(&file);
    ts << "# Mcaster1 DSP Encoder — Per-Unit Effect Config\n";
    ts << "# Auto-generated — do not edit while encoder is running\n\n";

    switch (unit) {
    case DspUnitId::AGC:
        ts << "unit_id: agc\n";
        ts << "preset: \"" << QString::fromStdString(dsp.agc_preset) << "\"\n\n";
        ts << "agc:\n";
        emit_kv(ts, "input_gain_db",     dsp.agc.input_gain_db);
        emit_kv(ts, "gate_threshold_db", dsp.agc.gate_threshold_db);
        emit_kv(ts, "threshold_db",      dsp.agc.threshold_db);
        emit_kv(ts, "ratio",             dsp.agc.ratio);
        emit_kv(ts, "attack_ms",         dsp.agc.attack_ms);
        emit_kv(ts, "release_ms",        dsp.agc.release_ms);
        emit_kv(ts, "knee_db",           dsp.agc.knee_db);
        emit_kv(ts, "makeup_gain_db",    dsp.agc.makeup_gain_db);
        emit_kv(ts, "limiter_db",        dsp.agc.limiter_db);
        break;

    case DspUnitId::SONIC_ENHANCER:
        ts << "unit_id: sonic_enhancer\n";
        ts << "preset: \"" << QString::fromStdString(dsp.sonic_preset) << "\"\n\n";
        ts << "sonic:\n";
        emit_kv(ts, "lo_contour",  dsp.sonic.lo_contour);
        emit_kv(ts, "process",     dsp.sonic.process);
        emit_kv(ts, "output_gain", dsp.sonic.output_gain);
        break;

    case DspUnitId::EQ10:
        ts << "unit_id: eq10\n";
        ts << "preset: \"" << QString::fromStdString(dsp.eq10_preset) << "\"\n\n";
        ts << "eq10:\n";
        emit_kv(ts, "linked", dsp.eq10.linked);
        for (int i = 0; i < 10; ++i) {
            const auto& b = dsp.eq10.bands[i];
            ts << "  band_" << i << ":\n";
            emit_kv4(ts, "type",    std::string(eq_band_type_tag(b.type)));
            emit_kv4(ts, "freq_hz", b.freq_hz);
            emit_kv4(ts, "gain_db", b.gain_db);
            emit_kv4(ts, "q",       b.q);
            emit_kv4(ts, "enabled", b.enabled);
        }
        break;

    case DspUnitId::EQ31:
        ts << "unit_id: eq31\n";
        ts << "preset: \"" << QString::fromStdString(dsp.eq31_preset) << "\"\n\n";
        ts << "eq31:\n";
        emit_kv(ts, "linked", dsp.eq31.linked);
        for (int i = 0; i < 31; ++i) {
            char key_l[16], key_r[16];
            snprintf(key_l, sizeof(key_l), "gain_l_%d", i);
            snprintf(key_r, sizeof(key_r), "gain_r_%d", i);
            emit_kv(ts, key_l, dsp.eq31.gains_l[i]);
            emit_kv(ts, key_r, dsp.eq31.gains_r[i]);
        }
        break;

    case DspUnitId::PTT_DUCK:
        ts << "unit_id: ptt_duck\n\n";
        ts << "ptt_duck:\n";
        emit_kv(ts, "duck_level_db", dsp.ptt_duck.duck_level_db);
        emit_kv(ts, "attack_ms",     dsp.ptt_duck.attack_ms);
        emit_kv(ts, "release_ms",    dsp.ptt_duck.release_ms);
        emit_kv(ts, "mic_gain_db",   dsp.ptt_duck.mic_gain_db);
        break;

    case DspUnitId::DBX_VOICE:
        ts << "unit_id: dbx_voice\n";
        ts << "preset: \"" << QString::fromStdString(dsp.dbx_preset) << "\"\n\n";
        ts << "dbx_voice:\n";
        ts << "  gate:\n";
        emit_kv4(ts, "threshold_db", dsp.dbx_voice.gate.threshold_db);
        emit_kv4(ts, "ratio",        dsp.dbx_voice.gate.ratio);
        emit_kv4(ts, "attack_ms",    dsp.dbx_voice.gate.attack_ms);
        emit_kv4(ts, "release_ms",   dsp.dbx_voice.gate.release_ms);
        emit_kv4(ts, "enabled",      dsp.dbx_voice.gate.enabled);
        ts << "  compressor:\n";
        emit_kv4(ts, "threshold_db", dsp.dbx_voice.compressor.threshold_db);
        emit_kv4(ts, "ratio",        dsp.dbx_voice.compressor.ratio);
        emit_kv4(ts, "attack_ms",    dsp.dbx_voice.compressor.attack_ms);
        emit_kv4(ts, "release_ms",   dsp.dbx_voice.compressor.release_ms);
        emit_kv4(ts, "enabled",      dsp.dbx_voice.compressor.enabled);
        ts << "  deesser:\n";
        emit_kv4(ts, "frequency_hz",  dsp.dbx_voice.deesser.frequency_hz);
        emit_kv4(ts, "threshold_db",  dsp.dbx_voice.deesser.threshold_db);
        emit_kv4(ts, "reduction_db",  dsp.dbx_voice.deesser.reduction_db);
        emit_kv4(ts, "bandwidth_q",   dsp.dbx_voice.deesser.bandwidth_q);
        emit_kv4(ts, "enabled",       dsp.dbx_voice.deesser.enabled);
        ts << "  lf_enhancer:\n";
        emit_kv4(ts, "frequency_hz", dsp.dbx_voice.lf_enhancer.frequency_hz);
        emit_kv4(ts, "amount_db",    dsp.dbx_voice.lf_enhancer.amount_db);
        emit_kv4(ts, "enabled",      dsp.dbx_voice.lf_enhancer.enabled);
        ts << "  hf_detail:\n";
        emit_kv4(ts, "frequency_hz", dsp.dbx_voice.hf_detail.frequency_hz);
        emit_kv4(ts, "amount_db",    dsp.dbx_voice.hf_detail.amount_db);
        emit_kv4(ts, "enabled",      dsp.dbx_voice.hf_detail.enabled);
        break;

    default: break;
    }

    file.close();
    qDebug() << "GlobalConfigManager: saved" << QString::fromStdString(path);
    return true;
}

/* ── Rack Enable State ──────────────────────────────────────────────── */

DspRackEnableState GlobalConfigManager::load_rack_enable_state()
{
    DspRackEnableState state;
    std::string path = config_dir() + "/" + kRackStateFilename;

    YamlDoc yd;
    yaml_node_t* root = parse_yaml_file(path, yd);
    if (!root) return state;

    state.eq10_enabled      = ybool(&yd.doc, root, "eq10_enabled");
    state.eq31_enabled      = ybool(&yd.doc, root, "eq31_enabled");
    state.sonic_enabled     = ybool(&yd.doc, root, "sonic_enabled");
    state.ptt_duck_enabled  = ybool(&yd.doc, root, "ptt_duck_enabled");
    state.agc_enabled       = ybool(&yd.doc, root, "agc_enabled");
    state.dbx_voice_enabled = ybool(&yd.doc, root, "dbx_voice_enabled");

    fprintf(stderr, "[M11.5] load_rack_enable_state: agc=%d sonic=%d eq10=%d eq31=%d ptt=%d dbx=%d from '%s'\n",
            state.agc_enabled, state.sonic_enabled, state.eq10_enabled,
            state.eq31_enabled, state.ptt_duck_enabled, state.dbx_voice_enabled,
            path.c_str());
    return state;
}

bool GlobalConfigManager::save_rack_enable_state(const DspRackEnableState& state)
{
    std::string path = config_dir() + "/" + kRackStateFilename;

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "GlobalConfigManager: cannot write" << QString::fromStdString(path);
        return false;
    }

    QTextStream ts(&file);
    ts << "# Mcaster1 DSP Encoder — Rack Enable State\n";
    ts << "# Auto-generated — do not edit while encoder is running\n\n";

    ts << "eq10_enabled: "      << (state.eq10_enabled      ? "true" : "false") << "\n";
    ts << "eq31_enabled: "      << (state.eq31_enabled      ? "true" : "false") << "\n";
    ts << "sonic_enabled: "     << (state.sonic_enabled     ? "true" : "false") << "\n";
    ts << "ptt_duck_enabled: "  << (state.ptt_duck_enabled  ? "true" : "false") << "\n";
    ts << "agc_enabled: "       << (state.agc_enabled       ? "true" : "false") << "\n";
    ts << "dbx_voice_enabled: " << (state.dbx_voice_enabled ? "true" : "false") << "\n";

    file.close();
    qDebug() << "GlobalConfigManager: saved rack enable state";
    return true;
}

} // namespace mc1
