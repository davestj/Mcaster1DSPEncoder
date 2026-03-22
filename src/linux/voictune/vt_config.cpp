/*
 * Mcaster1 VoicTune — YAML Config Loader
 * voictune/vt_config.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vt_config.h"
#include "vt_logger.h"
#include <yaml.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace mc1vt {

/* We use a simple libyaml event-based parser to fill VtConfig.
 * The YAML structure is flat enough that we track section + key. */

bool vt_load_config(const std::string& yaml_path, VtConfig& cfg)
{
    FILE* fp = fopen(yaml_path.c_str(), "r");
    if (!fp) {
        VT_ERR("Cannot open config file: " + yaml_path);
        return false;
    }

    yaml_parser_t parser;
    yaml_event_t  event;
    if (!yaml_parser_initialize(&parser)) {
        fclose(fp);
        VT_ERR("yaml_parser_initialize failed");
        return false;
    }
    yaml_parser_set_input_file(&parser, fp);

    std::string section;
    std::string key;
    bool expect_value = false;
    int depth = 0;

    while (true) {
        if (!yaml_parser_parse(&parser, &event)) {
            VT_ERR("YAML parse error in " + yaml_path + ": " + std::string(parser.problem ? parser.problem : "unknown"));
            yaml_parser_delete(&parser);
            fclose(fp);
            return false;
        }

        if (event.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }

        if (event.type == YAML_MAPPING_START_EVENT) {
            ++depth;
        } else if (event.type == YAML_MAPPING_END_EVENT) {
            if (depth == 2) section.clear();
            --depth;
        } else if (event.type == YAML_SCALAR_EVENT) {
            std::string val((const char*)event.data.scalar.value, event.data.scalar.length);

            if (!expect_value) {
                /* This is a key */
                if (depth == 1) {
                    /* Top-level section ignored (we expect "voictune:" wrapper) */
                } else if (depth == 2) {
                    section = val;
                } else if (depth == 3) {
                    key = val;
                    expect_value = true;
                }
            } else {
                /* This is a value */
                expect_value = false;

                if (section == "http") {
                    if      (key == "port")     cfg.http.port     = atoi(val.c_str());
                    else if (key == "ssl-port")  cfg.http.ssl_port = atoi(val.c_str());
                    else if (key == "bind")      cfg.http.bind     = val;
                    else if (key == "ssl-cert")  cfg.http.ssl_cert = val;
                    else if (key == "ssl-key")   cfg.http.ssl_key  = val;
                }
                else if (section == "audio") {
                    if      (key == "input-device-index")  cfg.audio.input_device_index  = atoi(val.c_str());
                    else if (key == "output-device-index") cfg.audio.output_device_index = atoi(val.c_str());
                    else if (key == "sample-rate")         cfg.audio.sample_rate         = atoi(val.c_str());
                    else if (key == "channels")            cfg.audio.channels            = atoi(val.c_str());
                    else if (key == "buffer-frames")       cfg.audio.buffer_frames       = atoi(val.c_str());
                    else if (key == "preferred-usb-id")    cfg.audio.preferred_usb_id    = val;
                    else if (key == "preferred-bt-addr")   cfg.audio.preferred_bt_addr   = val;
                    else if (key == "hotplug-enabled")     cfg.audio.hotplug_enabled     = (val == "true" || val == "1");
                    else if (key == "hotplug-settle-ms")   cfg.audio.hotplug_settle_ms   = atoi(val.c_str());
                    else if (key == "monitor-output")      cfg.audio.monitor_output      = (val == "true" || val == "1");
                }
                else if (section == "websocket") {
                    if      (key == "port")        cfg.websocket.port        = atoi(val.c_str());
                    else if (key == "max-clients")  cfg.websocket.max_clients = atoi(val.c_str());
                }
                else if (section == "analysis") {
                    if      (key == "fft-size")        cfg.analysis.fft_size       = atoi(val.c_str());
                    else if (key == "hop-size")         cfg.analysis.hop_size       = atoi(val.c_str());
                    else if (key == "lufs-window-ms")   cfg.analysis.lufs_window_ms = atoi(val.c_str());
                }
                else if (section == "ollama") {
                    if      (key == "endpoint")    cfg.ollama.endpoint    = val;
                    else if (key == "model")        cfg.ollama.model       = val;
                    else if (key == "timeout-sec")  cfg.ollama.timeout_sec = atoi(val.c_str());
                }
                else if (section == "database") {
                    if      (key == "host")     cfg.database.host     = val;
                    else if (key == "port")      cfg.database.port     = atoi(val.c_str());
                    else if (key == "user")      cfg.database.user     = val;
                    else if (key == "password")   cfg.database.password = val;
                    else if (key == "db_name")    cfg.database.db_name  = val;
                }
                else if (section == "log") {
                    if      (key == "dir")   cfg.log.dir   = val;
                    else if (key == "level")  cfg.log.level = atoi(val.c_str());
                }
                else if (section == "auth") {
                    if      (key == "username")           cfg.auth.username            = val;
                    else if (key == "password")            cfg.auth.password            = val;
                    else if (key == "api-token")           cfg.auth.api_token           = val;
                    else if (key == "session-timeout-sec") cfg.auth.session_timeout_sec = atoi(val.c_str());
                }
            }
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    fclose(fp);

    VT_INFO("Loaded config from " + yaml_path);
    return true;
}

} // namespace mc1vt
