/*
 * Mcaster1 Producer — YAML Config Loader
 * producer/pr_config.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pr_config.h"
#include "pr_logger.h"
#include <yaml.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace mc1pr {

bool pr_load_config(const std::string& yaml_path, PrConfig& cfg)
{
    FILE* fp = fopen(yaml_path.c_str(), "r");
    if (!fp) {
        PR_ERR("Cannot open config file: " + yaml_path);
        return false;
    }

    yaml_parser_t parser;
    yaml_event_t  event;
    if (!yaml_parser_initialize(&parser)) {
        fclose(fp);
        PR_ERR("yaml_parser_initialize failed");
        return false;
    }
    yaml_parser_set_input_file(&parser, fp);

    std::string section;
    std::string key;
    bool expect_value = false;
    int depth = 0;

    while (true) {
        if (!yaml_parser_parse(&parser, &event)) {
            PR_ERR("YAML parse error in " + yaml_path + ": " +
                   std::string(parser.problem ? parser.problem : "unknown"));
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
                    /* Top-level section ignored (we expect "producer:" wrapper) */
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
                    if      (key == "port")      cfg.http.port     = atoi(val.c_str());
                    else if (key == "ssl-port")   cfg.http.ssl_port = atoi(val.c_str());
                    else if (key == "bind")       cfg.http.bind     = val;
                    else if (key == "ssl-cert")   cfg.http.ssl_cert = val;
                    else if (key == "ssl-key")    cfg.http.ssl_key  = val;
                }
                else if (section == "workers") {
                    if      (key == "video-threads") cfg.workers.video_threads = atoi(val.c_str());
                    else if (key == "audio-threads") cfg.workers.audio_threads = atoi(val.c_str());
                    else if (key == "fft-threads")   cfg.workers.fft_threads   = atoi(val.c_str());
                }
                else if (section == "auth") {
                    if      (key == "username")            cfg.auth.username            = val;
                    else if (key == "password")             cfg.auth.password            = val;
                    else if (key == "api-token")            cfg.auth.api_token           = val;
                    else if (key == "session-timeout-sec")  cfg.auth.session_timeout_sec = atoi(val.c_str());
                }
                else if (section == "log") {
                    if      (key == "dir")   cfg.log.dir   = val;
                    else if (key == "level")  cfg.log.level = atoi(val.c_str());
                }
            }
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    fclose(fp);

    PR_INFO("Loaded config from " + yaml_path);
    return true;
}

} // namespace mc1pr
