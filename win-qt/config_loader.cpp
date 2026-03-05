/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * config_loader.cpp — YAML configuration loader (ported from Linux)
 *
 * Parses YAML config files into mc1 config structs.
 * No MySQL dependency — macOS build stores all config in YAML.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config_loader.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <yaml.h>

namespace mc1 {

/* ── YAML parsing helpers ──────────────────────────────────────────── */

struct YamlDoc {
    yaml_document_t doc;
    bool valid = false;

    ~YamlDoc() { if (valid) yaml_document_delete(&doc); }
};

static const char* node_scalar(yaml_document_t *doc, yaml_node_t *node)
{
    if (!node || node->type != YAML_SCALAR_NODE) return "";
    return reinterpret_cast<const char*>(node->data.scalar.value);
}

static yaml_node_t* map_get(yaml_document_t *doc, yaml_node_t *map, const char *key)
{
    if (!map || map->type != YAML_MAPPING_NODE) return nullptr;
    for (auto *p = map->data.mapping.pairs.start; p < map->data.mapping.pairs.top; ++p) {
        yaml_node_t *k = yaml_document_get_node(doc, p->key);
        if (k && k->type == YAML_SCALAR_NODE) {
            if (strcmp(reinterpret_cast<const char*>(k->data.scalar.value), key) == 0)
                return yaml_document_get_node(doc, p->value);
        }
    }
    return nullptr;
}

static bool yaml_bool(const char *s)
{
    return (strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 || strcmp(s, "1") == 0);
}

static int yaml_int(const char *s, int def = 0)
{
    if (!s || !*s) return def;
    char *end = nullptr;
    long v = strtol(s, &end, 10);
    return (end != s) ? static_cast<int>(v) : def;
}

static std::string yaml_str(yaml_document_t *doc, yaml_node_t *map, const char *key)
{
    yaml_node_t *n = map_get(doc, map, key);
    return n ? std::string(node_scalar(doc, n)) : std::string();
}

static int yaml_int_key(yaml_document_t *doc, yaml_node_t *map, const char *key, int def = 0)
{
    yaml_node_t *n = map_get(doc, map, key);
    return n ? yaml_int(node_scalar(doc, n), def) : def;
}

static bool yaml_bool_key(yaml_document_t *doc, yaml_node_t *map, const char *key, bool def = false)
{
    yaml_node_t *n = map_get(doc, map, key);
    return n ? yaml_bool(node_scalar(doc, n)) : def;
}

/* ── Parse sections ────────────────────────────────────────────────── */

static void parse_http_admin(yaml_document_t *doc, yaml_node_t *node, AdminConfig &cfg)
{
    cfg.username  = yaml_str(doc, node, "username");
    cfg.password  = yaml_str(doc, node, "password");
    cfg.api_token = yaml_str(doc, node, "api_token");
    cfg.log_dir   = yaml_str(doc, node, "log-dir");
    cfg.log_level = yaml_int_key(doc, node, "log-level", 4);
    cfg.session_timeout = yaml_int_key(doc, node, "session-timeout", 3600);
}

static void parse_sockets(yaml_document_t *doc, yaml_node_t *seq, AdminConfig &cfg)
{
    if (!seq || seq->type != YAML_SEQUENCE_NODE) return;
    for (auto *it = seq->data.sequence.items.start; it < seq->data.sequence.items.top; ++it) {
        yaml_node_t *item = yaml_document_get_node(doc, *it);
        if (!item || item->type != YAML_MAPPING_NODE) continue;

        SocketConfig sc;
        sc.port         = static_cast<uint16_t>(yaml_int_key(doc, item, "port", 8330));
        sc.bind_address = yaml_str(doc, item, "bind_address");
        sc.ssl_enabled  = yaml_bool_key(doc, item, "ssl_enabled");
        sc.ssl_cert     = yaml_str(doc, item, "ssl_cert");
        sc.ssl_key      = yaml_str(doc, item, "ssl_key");
        cfg.sockets.push_back(sc);
    }
}

static void parse_dnas(yaml_document_t *doc, yaml_node_t *node, DnasConfig &cfg)
{
    cfg.host      = yaml_str(doc, node, "host");
    cfg.port      = static_cast<uint16_t>(yaml_int_key(doc, node, "port", 8000));
    cfg.username  = yaml_str(doc, node, "username");
    cfg.password  = yaml_str(doc, node, "password");
    cfg.stats_url = yaml_str(doc, node, "stats_url");
    cfg.ssl       = yaml_bool_key(doc, node, "ssl");
}

static EncoderConfig::Codec parse_codec(const std::string &s)
{
    if (s == "mp3")      return EncoderConfig::Codec::MP3;
    if (s == "vorbis")   return EncoderConfig::Codec::VORBIS;
    if (s == "opus")     return EncoderConfig::Codec::OPUS;
    if (s == "flac")     return EncoderConfig::Codec::FLAC;
    if (s == "aac_lc")   return EncoderConfig::Codec::AAC_LC;
    if (s == "aac_he")   return EncoderConfig::Codec::AAC_HE;
    if (s == "aac_hev2") return EncoderConfig::Codec::AAC_HE_V2;
    if (s == "aac_eld")  return EncoderConfig::Codec::AAC_ELD;
    return EncoderConfig::Codec::MP3;
}

static StreamTarget::Protocol parse_protocol(const std::string &s)
{
    if (s == "shoutcast_v1") return StreamTarget::Protocol::SHOUTCAST_V1;
    if (s == "shoutcast_v2") return StreamTarget::Protocol::SHOUTCAST_V2;
    if (s == "live365")      return StreamTarget::Protocol::LIVE365;
    return StreamTarget::Protocol::ICECAST2;
}

static void parse_stream_target(yaml_document_t *doc, yaml_node_t *node, StreamTarget &t)
{
    t.protocol     = parse_protocol(yaml_str(doc, node, "protocol"));
    t.host         = yaml_str(doc, node, "host");
    t.port         = static_cast<uint16_t>(yaml_int_key(doc, node, "port", 8000));
    t.mount        = yaml_str(doc, node, "mount");
    t.username     = yaml_str(doc, node, "username");
    t.password     = yaml_str(doc, node, "password");
    t.station_name = yaml_str(doc, node, "station_name");
    t.description  = yaml_str(doc, node, "description");
    t.genre        = yaml_str(doc, node, "genre");
    t.url          = yaml_str(doc, node, "url");
    t.content_type = yaml_str(doc, node, "content_type");
    t.bitrate      = yaml_int_key(doc, node, "bitrate", 128);
    t.sample_rate  = yaml_int_key(doc, node, "sample_rate", 44100);
    t.channels     = yaml_int_key(doc, node, "channels", 2);
}

static void parse_icy22(yaml_document_t *doc, yaml_node_t *node, ICY22Config &c)
{
    c.session_id    = yaml_str(doc, node, "session_id");
    c.station_id    = yaml_str(doc, node, "station_id");
    c.station_logo  = yaml_str(doc, node, "station_logo");
    c.verify_status = yaml_str(doc, node, "verify_status");
    c.show_title    = yaml_str(doc, node, "show_title");
    c.show_start    = yaml_str(doc, node, "show_start");
    c.show_end      = yaml_str(doc, node, "show_end");
    c.next_show     = yaml_str(doc, node, "next_show");
    c.next_show_time = yaml_str(doc, node, "next_show_time");
    c.schedule_url  = yaml_str(doc, node, "schedule_url");
    c.auto_dj       = yaml_bool_key(doc, node, "auto_dj");
    c.playlist_name = yaml_str(doc, node, "playlist_name");
    c.dj_handle     = yaml_str(doc, node, "dj_handle");
    c.dj_bio        = yaml_str(doc, node, "dj_bio");
    c.dj_genre      = yaml_str(doc, node, "dj_genre");
    c.dj_rating     = yaml_str(doc, node, "dj_rating");
    c.creator_handle = yaml_str(doc, node, "creator_handle");
    c.social_twitter  = yaml_str(doc, node, "social_twitter");
    c.social_twitch   = yaml_str(doc, node, "social_twitch");
    c.social_instagram = yaml_str(doc, node, "social_instagram");
    c.social_tiktok   = yaml_str(doc, node, "social_tiktok");
    c.social_youtube  = yaml_str(doc, node, "social_youtube");
    c.social_facebook = yaml_str(doc, node, "social_facebook");
    c.social_linkedin = yaml_str(doc, node, "social_linkedin");
    c.social_linktree = yaml_str(doc, node, "social_linktree");
    c.emoji          = yaml_str(doc, node, "emoji");
    c.hashtags       = yaml_str(doc, node, "hashtags");
    c.request_enabled = yaml_bool_key(doc, node, "request_enabled");
    c.request_url    = yaml_str(doc, node, "request_url");
    c.chat_url       = yaml_str(doc, node, "chat_url");
    c.tip_url        = yaml_str(doc, node, "tip_url");
    c.events_url     = yaml_str(doc, node, "events_url");
    c.crosspost_platforms = yaml_str(doc, node, "crosspost_platforms");
    c.cdn_region     = yaml_str(doc, node, "cdn_region");
    c.relay_origin   = yaml_str(doc, node, "relay_origin");
    c.nsfw           = yaml_bool_key(doc, node, "nsfw");
    c.ai_generated   = yaml_bool_key(doc, node, "ai_generated");
    c.royalty_free   = yaml_bool_key(doc, node, "royalty_free");
    c.license_type   = yaml_str(doc, node, "license_type");
    c.license_territory = yaml_str(doc, node, "license_territory");
    c.geo_region     = yaml_str(doc, node, "geo_region");
    c.notice_text    = yaml_str(doc, node, "notice_text");
    c.notice_url     = yaml_str(doc, node, "notice_url");
    c.notice_expires = yaml_str(doc, node, "notice_expires");
    c.loudness_lufs  = yaml_str(doc, node, "loudness_lufs");
    c.video_type     = yaml_str(doc, node, "video_type");
    c.video_link     = yaml_str(doc, node, "video_link");
    c.video_title    = yaml_str(doc, node, "video_title");
    c.video_poster   = yaml_str(doc, node, "video_poster");
    c.video_platform = yaml_str(doc, node, "video_platform");
    c.video_live     = yaml_bool_key(doc, node, "video_live");
    c.video_codec    = yaml_str(doc, node, "video_codec");
    c.video_fps      = yaml_str(doc, node, "video_fps");
    c.video_resolution = yaml_str(doc, node, "video_resolution");
    c.video_rating   = yaml_str(doc, node, "video_rating");
    c.video_nsfw     = yaml_bool_key(doc, node, "video_nsfw");
}

static void parse_slot(yaml_document_t *doc, yaml_node_t *node, EncoderConfig &cfg)
{
    cfg.slot_id      = yaml_int_key(doc, node, "slot_id", 0);
    cfg.name         = yaml_str(doc, node, "name");
    cfg.codec        = parse_codec(yaml_str(doc, node, "codec"));
    cfg.bitrate_kbps = yaml_int_key(doc, node, "bitrate_kbps", 128);
    cfg.quality      = yaml_int_key(doc, node, "quality", 5);
    cfg.sample_rate  = yaml_int_key(doc, node, "sample_rate", 44100);
    cfg.channels     = yaml_int_key(doc, node, "channels", 2);

    std::string em = yaml_str(doc, node, "encode_mode");
    if (em == "vbr") cfg.encode_mode = EncoderConfig::EncodeMode::VBR;
    else if (em == "abr") cfg.encode_mode = EncoderConfig::EncodeMode::ABR;
    else cfg.encode_mode = EncoderConfig::EncodeMode::CBR;

    std::string cm = yaml_str(doc, node, "channel_mode");
    if (cm == "stereo") cfg.channel_mode = EncoderConfig::ChannelMode::STEREO;
    else if (cm == "mono") cfg.channel_mode = EncoderConfig::ChannelMode::MONO;
    else cfg.channel_mode = EncoderConfig::ChannelMode::JOINT;

    std::string it = yaml_str(doc, node, "input_type");
    if (it == "device") cfg.input_type = EncoderConfig::InputType::DEVICE;
    else if (it == "url") cfg.input_type = EncoderConfig::InputType::URL;
    else cfg.input_type = EncoderConfig::InputType::PLAYLIST;

    cfg.device_index  = yaml_int_key(doc, node, "device_index", -1);
    cfg.playlist_path = yaml_str(doc, node, "playlist_path");

    cfg.dsp_eq_enabled         = yaml_bool_key(doc, node, "dsp_eq_enabled");
    cfg.dsp_agc_enabled        = yaml_bool_key(doc, node, "dsp_agc_enabled");
    cfg.dsp_crossfade_enabled  = yaml_bool_key(doc, node, "dsp_crossfade_enabled", true);
    cfg.dsp_eq_preset          = yaml_str(doc, node, "dsp_eq_preset");
    cfg.dsp_ptt_duck_enabled   = yaml_bool_key(doc, node, "dsp_ptt_duck_enabled");
    cfg.dsp_dbx_voice_enabled  = yaml_bool_key(doc, node, "dsp_dbx_voice_enabled");
    cfg.shuffle                = yaml_bool_key(doc, node, "shuffle");
    cfg.repeat_all             = yaml_bool_key(doc, node, "repeat_all", true);
    cfg.archive_enabled        = yaml_bool_key(doc, node, "archive_enabled");
    cfg.archive_wav            = yaml_bool_key(doc, node, "archive_wav");
    cfg.archive_dir            = yaml_str(doc, node, "archive_dir");
    cfg.auto_start             = yaml_bool_key(doc, node, "auto_start");
    cfg.auto_reconnect         = yaml_bool_key(doc, node, "auto_reconnect", true);
    cfg.reconnect_interval_sec = yaml_int_key(doc, node, "reconnect_interval_sec", 5);
    cfg.reconnect_max_attempts = yaml_int_key(doc, node, "reconnect_max_attempts", 0);

    /* Stream target */
    yaml_node_t *st = map_get(doc, node, "stream_target");
    if (st) parse_stream_target(doc, st, cfg.stream_target);

    /* ICY 2.2 */
    yaml_node_t *icy = map_get(doc, node, "icy22");
    if (icy) parse_icy22(doc, icy, cfg.icy22);

    /* YP */
    yaml_node_t *yp = map_get(doc, node, "yp");
    if (yp) {
        cfg.yp.is_public    = yaml_bool_key(doc, yp, "is_public");
        cfg.yp.stream_name  = yaml_str(doc, yp, "stream_name");
        cfg.yp.stream_desc  = yaml_str(doc, yp, "stream_desc");
        cfg.yp.stream_url   = yaml_str(doc, yp, "stream_url");
        cfg.yp.stream_genre = yaml_str(doc, yp, "stream_genre");
        cfg.yp.icq          = yaml_str(doc, yp, "icq");
        cfg.yp.aim          = yaml_str(doc, yp, "aim");
        cfg.yp.irc          = yaml_str(doc, yp, "irc");
    }

    /* Podcast */
    yaml_node_t *pod = map_get(doc, node, "podcast");
    if (pod) {
        cfg.podcast.generate_rss    = yaml_bool_key(doc, pod, "generate_rss");
        cfg.podcast.use_yp_settings = yaml_bool_key(doc, pod, "use_yp_settings");
        cfg.podcast.title           = yaml_str(doc, pod, "title");
        cfg.podcast.author          = yaml_str(doc, pod, "author");
        cfg.podcast.category        = yaml_str(doc, pod, "category");
        cfg.podcast.language        = yaml_str(doc, pod, "language");
        cfg.podcast.copyright       = yaml_str(doc, pod, "copyright");
        cfg.podcast.website         = yaml_str(doc, pod, "website");
        cfg.podcast.cover_art_url   = yaml_str(doc, pod, "cover_art_url");
        cfg.podcast.description     = yaml_str(doc, pod, "description");
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

bool loadConfig(const QString &yaml_path,
                AdminConfig &admin_out,
                std::vector<EncoderConfig> &slots_out,
                QString &error_out)
{
    QFile file(yaml_path);
    if (!file.open(QIODevice::ReadOnly)) {
        error_out = QStringLiteral("Cannot open config file: ") + yaml_path;
        return false;
    }
    QByteArray data = file.readAll();
    file.close();

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        error_out = QStringLiteral("Failed to initialize YAML parser");
        return false;
    }
    yaml_parser_set_input_string(&parser,
        reinterpret_cast<const unsigned char*>(data.constData()),
        static_cast<size_t>(data.size()));

    YamlDoc ydoc;
    if (!yaml_parser_load(&parser, &ydoc.doc)) {
        error_out = QString("YAML parse error at line %1: %2")
            .arg(parser.problem_mark.line + 1)
            .arg(parser.problem ? parser.problem : "unknown");
        yaml_parser_delete(&parser);
        return false;
    }
    ydoc.valid = true;
    yaml_parser_delete(&parser);

    yaml_node_t *root = yaml_document_get_node(&ydoc.doc, 1);
    if (!root || root->type != YAML_MAPPING_NODE) {
        error_out = QStringLiteral("YAML root is not a mapping");
        return false;
    }

    /* http-admin section */
    yaml_node_t *ha = map_get(&ydoc.doc, root, "http-admin");
    if (ha) parse_http_admin(&ydoc.doc, ha, admin_out);

    /* sockets section */
    yaml_node_t *socks = map_get(&ydoc.doc, root, "sockets");
    if (socks) parse_sockets(&ydoc.doc, socks, admin_out);

    /* dnas section */
    yaml_node_t *dnas_node = map_get(&ydoc.doc, root, "dnas");
    if (dnas_node) parse_dnas(&ydoc.doc, dnas_node, admin_out.dnas);

    /* webroot */
    yaml_node_t *wr = map_get(&ydoc.doc, root, "webroot");
    if (wr) admin_out.webroot = node_scalar(&ydoc.doc, wr);

    /* encoder-slots (sequence of mappings) */
    slots_out.clear();
    yaml_node_t *enc = map_get(&ydoc.doc, root, "encoder-slots");
    if (enc && enc->type == YAML_SEQUENCE_NODE) {
        for (auto *it = enc->data.sequence.items.start;
             it < enc->data.sequence.items.top; ++it) {
            yaml_node_t *slot_node = yaml_document_get_node(&ydoc.doc, *it);
            if (!slot_node || slot_node->type != YAML_MAPPING_NODE) continue;
            EncoderConfig cfg;
            parse_slot(&ydoc.doc, slot_node, cfg);
            slots_out.push_back(std::move(cfg));
        }
    }

    return true;
}

bool saveSlots(const QString &yaml_path,
               const std::vector<EncoderConfig> &/*slot_configs*/,
               QString &error_out)
{
    /* TODO Phase M2+: serialize encoder slots back to YAML */
    error_out = QStringLiteral("Save not yet implemented");
    Q_UNUSED(yaml_path);
    return false;
}

} // namespace mc1
