// config_loader.cpp — Startup YAML → gAdminConfig, then MySQL → pipeline slots
//
// We maintain a strict two-phase startup:
//   Phase 1 (YAML):  minimal startup config only — HTTP sockets, SSL,
//                    admin credentials, log settings, database connection,
//                    DNAS proxy target, webroot.
//   Phase 2 (MySQL): encoder slots loaded from mcaster1_encoder.encoder_configs
//                    via mc1_load_slots_from_db().
//
// This keeps sensitive config (DB credentials) in one place (the YAML), and
// all operational config (what to encode, where to stream) in the database
// where the web UI can manage it at runtime.
//
// CHANGELOG:
//   2026-02-24  Refactored: removed encoder slot YAML parsing, added MySQL
//               slot loader, added database: and dnas: YAML sections.

#include "config_loader.h"
#include "../platform.h"
#include "../libmcaster1dspencoder/libmcaster1dspencoder.h"
#include "audio_pipeline.h"
#include "encoder_slot.h"
#include "stream_client.h"
#include "mc1_logger.h"

#include <yaml.h>
#include <mysql.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string>

// We define gAdminConfig here; it is declared extern in libmcaster1dspencoder.h
mc1AdminConfig gAdminConfig;

// ---------------------------------------------------------------------------
// Internal YAML helpers
// ---------------------------------------------------------------------------

static const char* node_scalar(yaml_document_t* doc, yaml_node_t* node)
{
    if (!node || node->type != YAML_SCALAR_NODE) return "";
    return reinterpret_cast<const char*>(node->data.scalar.value);
}

static yaml_node_t* map_get(yaml_document_t* doc,
                             yaml_node_t*     map,
                             const char*      key)
{
    if (!map || map->type != YAML_MAPPING_NODE) return nullptr;
    for (auto* pair = map->data.mapping.pairs.start;
         pair < map->data.mapping.pairs.top; ++pair)
    {
        yaml_node_t* k = yaml_document_get_node(doc, pair->key);
        if (k && k->type == YAML_SCALAR_NODE &&
            strcmp(reinterpret_cast<const char*>(k->data.scalar.value), key) == 0)
        {
            return yaml_document_get_node(doc, pair->value);
        }
    }
    return nullptr;
}

static bool yaml_bool(const char* v)
{
    return v && (strcmp(v,"true")==0 || strcmp(v,"yes")==0 || strcmp(v,"1")==0);
}

// ---------------------------------------------------------------------------
// http-admin section → gAdminConfig
// ---------------------------------------------------------------------------

static void parse_http_admin(yaml_document_t* doc,
                              yaml_node_t*     node,
                              std::string*     webroot_out)
{
    if (!node || node->type != YAML_MAPPING_NODE) return;

    yaml_node_t* v;

    if ((v = map_get(doc, node, "admin-username")))
        strncpy(gAdminConfig.admin_username, node_scalar(doc, v),
                sizeof(gAdminConfig.admin_username) - 1);

    if ((v = map_get(doc, node, "admin-password")))
        strncpy(gAdminConfig.admin_password, node_scalar(doc, v),
                sizeof(gAdminConfig.admin_password) - 1);

    if ((v = map_get(doc, node, "api-token")))
        strncpy(gAdminConfig.admin_api_token, node_scalar(doc, v),
                sizeof(gAdminConfig.admin_api_token) - 1);

    if ((v = map_get(doc, node, "session-timeout")))
        gAdminConfig.session_timeout_secs = atoi(node_scalar(doc, v));

    if ((v = map_get(doc, node, "log-dir")))
        strncpy(gAdminConfig.log_dir, node_scalar(doc, v),
                sizeof(gAdminConfig.log_dir) - 1);

    if ((v = map_get(doc, node, "log-level")))
        gAdminConfig.log_level = atoi(node_scalar(doc, v));

    if (webroot_out) {
        if ((v = map_get(doc, node, "webroot"))) {
            const char* wr = node_scalar(doc, v);
            if (wr && wr[0]) *webroot_out = wr;
        }
    }

    // Sockets array
    yaml_node_t* sockets_node = map_get(doc, node, "sockets");
    if (!sockets_node || sockets_node->type != YAML_SEQUENCE_NODE) return;

    gAdminConfig.num_sockets = 0;
    for (auto* item = sockets_node->data.sequence.items.start;
         item < sockets_node->data.sequence.items.top &&
         gAdminConfig.num_sockets < MC1_MAX_LISTENERS; ++item)
    {
        yaml_node_t* sock_node = yaml_document_get_node(doc, *item);
        if (!sock_node || sock_node->type != YAML_MAPPING_NODE) continue;

        mc1ListenSocket& s = gAdminConfig.sockets[gAdminConfig.num_sockets];
        memset(&s, 0, sizeof(s));

        if ((v = map_get(doc, sock_node, "bind")))
            strncpy(s.bind_address, node_scalar(doc, v), sizeof(s.bind_address)-1);
        else
            strncpy(s.bind_address, "0.0.0.0", sizeof(s.bind_address)-1);

        if ((v = map_get(doc, sock_node, "port")))
            s.port = atoi(node_scalar(doc, v));

        if ((v = map_get(doc, sock_node, "ssl")))
            s.ssl_enabled = yaml_bool(node_scalar(doc, v)) ? 1 : 0;

        if ((v = map_get(doc, sock_node, "cert")))
            strncpy(s.ssl_cert, node_scalar(doc, v), sizeof(s.ssl_cert)-1);
        if ((v = map_get(doc, sock_node, "key")))
            strncpy(s.ssl_key, node_scalar(doc, v), sizeof(s.ssl_key)-1);

        gAdminConfig.num_sockets++;
    }
}

// ---------------------------------------------------------------------------
// database: section → gAdminConfig.db
// ---------------------------------------------------------------------------

static void parse_database(yaml_document_t* doc, yaml_node_t* node)
{
    if (!node || node->type != YAML_MAPPING_NODE) return;

    yaml_node_t* v;
    mc1DbConfig& db = gAdminConfig.db;

    // We set sensible defaults before parsing
    strncpy(db.host,       "127.0.0.1",         sizeof(db.host)-1);
    db.port = 3306;
    strncpy(db.db_encoder, "mcaster1_encoder",  sizeof(db.db_encoder)-1);
    strncpy(db.db_media,   "mcaster1_media",    sizeof(db.db_media)-1);
    strncpy(db.db_metrics, "mcaster1_metrics",  sizeof(db.db_metrics)-1);

    if ((v = map_get(doc, node, "host")))       strncpy(db.host,       node_scalar(doc,v), sizeof(db.host)-1);
    if ((v = map_get(doc, node, "port")))       db.port = atoi(node_scalar(doc,v));
    if ((v = map_get(doc, node, "user")))       strncpy(db.user,       node_scalar(doc,v), sizeof(db.user)-1);
    if ((v = map_get(doc, node, "password")))   strncpy(db.password,   node_scalar(doc,v), sizeof(db.password)-1);
    if ((v = map_get(doc, node, "db_encoder"))) strncpy(db.db_encoder, node_scalar(doc,v), sizeof(db.db_encoder)-1);
    if ((v = map_get(doc, node, "db_media")))   strncpy(db.db_media,   node_scalar(doc,v), sizeof(db.db_media)-1);
    if ((v = map_get(doc, node, "db_metrics"))) strncpy(db.db_metrics, node_scalar(doc,v), sizeof(db.db_metrics)-1);
}

// ---------------------------------------------------------------------------
// dnas: section → gAdminConfig.dnas
// ---------------------------------------------------------------------------

static void parse_dnas(yaml_document_t* doc, yaml_node_t* node)
{
    if (!node || node->type != YAML_MAPPING_NODE) return;

    yaml_node_t* v;
    mc1DnasConfig& d = gAdminConfig.dnas;

    // Defaults
    strncpy(d.host,      "dnas.mcaster1.com",    sizeof(d.host)-1);
    d.port = 9443;
    strncpy(d.stats_url, "/admin/mcaster1stats",  sizeof(d.stats_url)-1);

    if ((v = map_get(doc, node, "host")))      strncpy(d.host,      node_scalar(doc,v), sizeof(d.host)-1);
    if ((v = map_get(doc, node, "port")))      d.port = atoi(node_scalar(doc,v));
    if ((v = map_get(doc, node, "stats_url"))) strncpy(d.stats_url, node_scalar(doc,v), sizeof(d.stats_url)-1);
    if ((v = map_get(doc, node, "username")))  strncpy(d.username,  node_scalar(doc,v), sizeof(d.username)-1);
    if ((v = map_get(doc, node, "password")))  strncpy(d.password,  node_scalar(doc,v), sizeof(d.password)-1);
}

// ---------------------------------------------------------------------------
// Public — load startup YAML: fills gAdminConfig (NO encoder slots)
// ---------------------------------------------------------------------------

bool mc1_load_config(const char*    path,
                     AudioPipeline* /* unused — slots come from DB now */,
                     std::string*   webroot_out)
{
    if (!path || !path[0]) {
        fprintf(stderr, "[config] No config file specified.\n");
        return false;
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[config] Cannot open: %s\n", path);
        return false;
    }

    yaml_parser_t   parser;
    yaml_document_t doc;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, f);

    if (!yaml_parser_load(&parser, &doc)) {
        fprintf(stderr, "[config] YAML parse error in %s: %s (line %zu)\n",
                path, parser.problem, parser.problem_mark.line + 1);
        yaml_parser_delete(&parser);
        fclose(f);
        return false;
    }

    yaml_node_t* root = yaml_document_get_root_node(&doc);
    if (!root || root->type != YAML_MAPPING_NODE) {
        fprintf(stderr, "[config] YAML root is not a mapping: %s\n", path);
        yaml_document_delete(&doc);
        yaml_parser_delete(&parser);
        fclose(f);
        return false;
    }

    yaml_node_t* n;

    // http-admin → gAdminConfig (sockets, SSL, credentials, log, webroot)
    if ((n = map_get(&doc, root, "http-admin")))
        parse_http_admin(&doc, n, webroot_out);
    else
        fprintf(stderr, "[config] Warning: no http-admin section in %s\n", path);

    // database → gAdminConfig.db
    if ((n = map_get(&doc, root, "database")))
        parse_database(&doc, n);
    else
        fprintf(stderr, "[config] Warning: no database section — using defaults\n");

    // dnas → gAdminConfig.dnas
    if ((n = map_get(&doc, root, "dnas")))
        parse_dnas(&doc, n);

    yaml_document_delete(&doc);
    yaml_parser_delete(&parser);
    fclose(f);

    fprintf(stdout, "[config] Loaded %s — %d HTTP socket(s), DB=%s@%s:%d\n",
            path, gAdminConfig.num_sockets,
            gAdminConfig.db.user, gAdminConfig.db.host, gAdminConfig.db.port);
    return true;
}

// ---------------------------------------------------------------------------
// mc1_load_slots_from_db — query encoder_configs → pipeline->add_slot()
//
// We use the libmariadb C API directly (no C++ ORM).
// Connection params come from gAdminConfig.db (populated by mc1_load_config).
// We only load rows where is_active = 1, ordered by slot_id.
// ---------------------------------------------------------------------------

bool mc1_load_slots_from_db(AudioPipeline* pipeline)
{
    if (!pipeline) return false;

    const mc1DbConfig& db = gAdminConfig.db;
    if (!db.user[0] || !db.host[0]) {
        fprintf(stderr, "[config] mc1_load_slots_from_db: database credentials not set\n");
        return false;
    }

    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        fprintf(stderr, "[config] mysql_init failed\n");
        return false;
    }

    // We reconnect automatically if the connection drops
    bool reconnect = true;
    mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

    if (!mysql_real_connect(conn,
                            db.host, db.user, db.password,
                            db.db_encoder, (unsigned int)db.port,
                            nullptr, 0))
    {
        fprintf(stderr, "[config] MySQL connect failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return false;
    }

    // We disable FK checks for safety and set utf8mb4
    mysql_query(conn, "SET NAMES utf8mb4");

    const char* sql =
        "SELECT slot_id, name, input_type, playlist_path, device_index,"
        "       codec, bitrate_kbps, sample_rate, channels,"
        "       COALESCE(quality,5)         AS quality,"
        "       COALESCE(encode_mode,'cbr') AS encode_mode,"
        "       COALESCE(channel_mode,'joint') AS channel_mode,"
        "       server_host, server_port, server_mount,"
        "       server_user, server_pass, server_protocol,"
        "       station_name, description, genre, website_url,"
        "       archive_enabled, archive_dir,"
        "       volume, shuffle, repeat_all,"
        "       eq_enabled, eq_preset, agc_enabled,"
        "       crossfade_enabled, crossfade_duration,"
        "       COALESCE(icy2_twitter,'')   AS icy2_twitter,"
        "       COALESCE(icy2_facebook,'')  AS icy2_facebook,"
        "       COALESCE(icy2_instagram,'') AS icy2_instagram,"
        "       COALESCE(icy2_email,'')     AS icy2_email,"
        "       COALESCE(icy2_language,'en') AS icy2_language,"
        "       COALESCE(icy2_country,'US')  AS icy2_country,"
        "       COALESCE(icy2_city,'')       AS icy2_city,"
        "       icy2_is_public,"
        "       COALESCE(auto_start,0)             AS auto_start,"
        "       COALESCE(auto_start_delay,0)       AS auto_start_delay,"
        "       COALESCE(auto_reconnect,1)         AS auto_reconnect,"
        "       COALESCE(reconnect_interval,5)     AS reconnect_interval,"
        "       COALESCE(reconnect_max_attempts,0) AS reconnect_max_attempts"
        "  FROM encoder_configs"
        " WHERE is_active = 1"
        " ORDER BY slot_id";

    if (mysql_query(conn, sql)) {
        fprintf(stderr, "[config] encoder_configs query failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "[config] mysql_store_result failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return false;
    }

    int slots_added = 0;
    MYSQL_ROW row;

    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        // We map column indices to match the SELECT order above
        int col = 0;
        EncoderConfig cfg;

        // slot_id, name
        cfg.slot_id = row[col] ? atoi(row[col]) : 0; col++;
        cfg.name    = row[col] ? row[col] : "";       col++;

        // input_type
        const char* itype = row[col] ? row[col] : "playlist"; col++;
        if      (strcmp(itype, "device")   == 0) cfg.input_type = EncoderConfig::InputType::DEVICE;
        else if (strcmp(itype, "url")      == 0) cfg.input_type = EncoderConfig::InputType::URL;
        else                                      cfg.input_type = EncoderConfig::InputType::PLAYLIST;

        // playlist_path, device_index
        cfg.playlist_path = row[col] ? row[col] : ""; col++;
        cfg.device_index  = row[col] ? atoi(row[col]) : -1; col++;

        // codec
        const char* codec = row[col] ? row[col] : "mp3"; col++;
        if      (strcmp(codec, "mp3")     == 0) cfg.codec = EncoderConfig::Codec::MP3;
        else if (strcmp(codec, "vorbis")  == 0) cfg.codec = EncoderConfig::Codec::VORBIS;
        else if (strcmp(codec, "opus")    == 0) cfg.codec = EncoderConfig::Codec::OPUS;
        else if (strcmp(codec, "flac")    == 0) cfg.codec = EncoderConfig::Codec::FLAC;
        else if (strcmp(codec, "aac_lc")  == 0) cfg.codec = EncoderConfig::Codec::AAC_LC;
        else if (strcmp(codec, "aac_he")  == 0) cfg.codec = EncoderConfig::Codec::AAC_HE;
        else if (strcmp(codec, "aac_hev2")== 0) cfg.codec = EncoderConfig::Codec::AAC_HE_V2;
        else if (strcmp(codec, "aac_eld") == 0) cfg.codec = EncoderConfig::Codec::AAC_ELD;
        else                                     cfg.codec = EncoderConfig::Codec::MP3;

        // bitrate, sample_rate, channels
        cfg.bitrate_kbps = row[col] ? atoi(row[col]) : 128; col++;
        cfg.sample_rate  = row[col] ? atoi(row[col]) : 44100; col++;
        cfg.channels     = row[col] ? atoi(row[col]) : 2; col++;

        // quality, encode_mode, channel_mode
        cfg.quality = row[col] ? atoi(row[col]) : 5; col++;

        const char* emode = row[col] ? row[col] : "cbr"; col++;
        if      (strcmp(emode, "vbr") == 0) cfg.encode_mode = EncoderConfig::EncodeMode::VBR;
        else if (strcmp(emode, "abr") == 0) cfg.encode_mode = EncoderConfig::EncodeMode::ABR;
        else                                cfg.encode_mode = EncoderConfig::EncodeMode::CBR;

        const char* cmode = row[col] ? row[col] : "joint"; col++;
        if      (strcmp(cmode, "stereo") == 0) cfg.channel_mode = EncoderConfig::ChannelMode::STEREO;
        else if (strcmp(cmode, "mono")   == 0) cfg.channel_mode = EncoderConfig::ChannelMode::MONO;
        else                                   cfg.channel_mode = EncoderConfig::ChannelMode::JOINT;

        // stream target: host, port, mount, user, pass, protocol
        StreamTarget& st = cfg.stream_target;
        st.host     = row[col] ? row[col] : ""; col++;
        st.port     = row[col] ? (uint16_t)atoi(row[col]) : 8000; col++;
        st.mount    = row[col] ? row[col] : ""; col++;
        st.username = row[col] ? row[col] : "source"; col++;
        st.password = row[col] ? row[col] : ""; col++;

        const char* proto = row[col] ? row[col] : "icecast2"; col++;
        if      (strcmp(proto, "shoutcast1") == 0) st.protocol = StreamTarget::Protocol::SHOUTCAST_V1;
        else if (strcmp(proto, "shoutcast2") == 0) st.protocol = StreamTarget::Protocol::SHOUTCAST_V2;
        else if (strcmp(proto, "mcaster1")   == 0) st.protocol = StreamTarget::Protocol::SHOUTCAST_V2;
        else                                        st.protocol = StreamTarget::Protocol::ICECAST2;

        // station info
        st.station_name = row[col] ? row[col] : ""; col++;
        st.description  = row[col] ? row[col] : ""; col++;
        st.genre        = row[col] ? row[col] : ""; col++;
        st.url          = row[col] ? row[col] : ""; col++;

        // archive
        cfg.archive_enabled = row[col] ? atoi(row[col]) != 0 : false; col++;
        cfg.archive_dir     = row[col] ? row[col] : ""; col++;

        // volume, shuffle, repeat
        cfg.volume     = row[col] ? (float)atof(row[col]) : 1.0f; col++;
        cfg.shuffle    = row[col] ? atoi(row[col]) != 0 : false; col++;
        cfg.repeat_all = row[col] ? atoi(row[col]) != 0 : true; col++;

        // DSP
        cfg.dsp_eq_enabled        = row[col] ? atoi(row[col]) != 0 : false; col++;
        cfg.dsp_eq_preset         = row[col] ? row[col] : "flat"; col++;
        cfg.dsp_agc_enabled       = row[col] ? atoi(row[col]) != 0 : false; col++;
        cfg.dsp_crossfade_enabled  = row[col] ? atoi(row[col]) != 0 : false; col++;
        cfg.dsp_crossfade_duration = row[col] ? (float)atof(row[col]) : 3.0f; col++;

        // ICY2 extended headers
        st.icy2_twitter   = row[col] ? row[col] : ""; col++;
        st.icy2_facebook  = row[col] ? row[col] : ""; col++;
        st.icy2_instagram = row[col] ? row[col] : ""; col++;
        st.icy2_email     = row[col] ? row[col] : ""; col++;
        st.icy2_language  = row[col] ? row[col] : "en"; col++;
        st.icy2_country   = row[col] ? row[col] : "US"; col++;
        st.icy2_city      = row[col] ? row[col] : ""; col++;
        st.icy2_is_public = row[col] ? atoi(row[col]) != 0 : true; col++;

        // auto_start, auto_start_delay, auto_reconnect, reconnect_interval, reconnect_max_attempts
        cfg.auto_start              = row[col] ? atoi(row[col]) != 0 : false; col++;
        cfg.auto_start_delay_sec    = row[col] ? atoi(row[col]) : 0; col++;
        cfg.auto_reconnect          = row[col] ? atoi(row[col]) != 0 : true; col++;
        cfg.reconnect_interval_sec  = row[col] ? atoi(row[col]) : 5; col++;
        cfg.reconnect_max_attempts  = row[col] ? atoi(row[col]) : 0; col++;

        // We derive MIME type from codec (init_aac overrides for AAC variants at runtime)
        switch (cfg.codec) {
            case EncoderConfig::Codec::MP3:       st.content_type = "audio/mpeg"; break;
            case EncoderConfig::Codec::VORBIS:    st.content_type = "audio/ogg";  break;
            case EncoderConfig::Codec::OPUS:      st.content_type = "audio/ogg";  break;
            case EncoderConfig::Codec::FLAC:      st.content_type = "audio/flac"; break;
            case EncoderConfig::Codec::AAC_LC:    st.content_type = "audio/aac";  break;
            case EncoderConfig::Codec::AAC_HE:    st.content_type = "audio/aacp"; break;
            case EncoderConfig::Codec::AAC_HE_V2: st.content_type = "audio/aacp"; break;
            case EncoderConfig::Codec::AAC_ELD:   st.content_type = "audio/aac";  break;
        }

        // Mirror codec params into StreamTarget for ICY header negotiation
        st.bitrate     = cfg.bitrate_kbps;
        st.sample_rate = cfg.sample_rate;
        st.channels    = cfg.channels;

        const char* codec_name =
            cfg.codec == EncoderConfig::Codec::MP3       ? "mp3"     :
            cfg.codec == EncoderConfig::Codec::VORBIS    ? "vorbis"  :
            cfg.codec == EncoderConfig::Codec::OPUS      ? "opus"    :
            cfg.codec == EncoderConfig::Codec::FLAC      ? "flac"    :
            cfg.codec == EncoderConfig::Codec::AAC_LC    ? "aac_lc"  :
            cfg.codec == EncoderConfig::Codec::AAC_HE    ? "aac_he"  :
            cfg.codec == EncoderConfig::Codec::AAC_HE_V2 ? "aac_hev2":
            cfg.codec == EncoderConfig::Codec::AAC_ELD   ? "aac_eld" : "unknown";

        pipeline->add_slot(cfg);
        fprintf(stdout, "[config] Slot %2d: %-42s  codec=%-8s  mount=%s\n",
                cfg.slot_id, cfg.name.c_str(), codec_name,
                cfg.stream_target.mount.c_str());
        slots_added++;
    }

    mysql_free_result(res);
    mysql_close(conn);

    fprintf(stdout, "[config] Loaded %d encoder slot(s) from MySQL\n", slots_added);
    return slots_added > 0;
}
