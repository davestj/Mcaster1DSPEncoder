// main_cli.cpp — mcaster1-encoder CLI entry point (Linux)
//
// Usage:
//   mcaster1-encoder [options]
//   mcaster1-encoder --ssl-gencert --ssl-gentype=selfsigned|csr \
//                    --subj="/C=US/ST=FL/CN=host" \
//                    --ssl-gencert-savepath=/etc/mcaster1/certs \
//                    [--ssl-gencert-addtoconfig=true] [-c <config>]

#include "../platform.h"
#include "../libmcaster1dspencoder/libmcaster1dspencoder.h"
#include "http_api.h"
#ifndef MC1_HTTP_TEST_BUILD
#include "audio_pipeline.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>

/* ── Signal handling ──────────────────────────────────────────────────────── */

static volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
    fprintf(stdout, "\n[encoder] Shutdown — stopping servers...\n");
    http_api_stop();
}

/* ── Default admin config ─────────────────────────────────────────────────── */

static void admin_defaults()
{
    memset(&gAdminConfig, 0, sizeof(gAdminConfig));
    gAdminConfig.enabled     = 1;
    gAdminConfig.num_sockets = 1;

    strncpy(gAdminConfig.sockets[0].bind_address, "127.0.0.1",
            sizeof(gAdminConfig.sockets[0].bind_address) - 1);
    gAdminConfig.sockets[0].port        = 8330;
    gAdminConfig.sockets[0].ssl_enabled = 0;

    strncpy(gAdminConfig.admin_username, "admin",
            sizeof(gAdminConfig.admin_username) - 1);
    strncpy(gAdminConfig.admin_password, "hackme",
            sizeof(gAdminConfig.admin_password) - 1);
    gAdminConfig.session_timeout_secs = 3600;
}

/* ── Usage ────────────────────────────────────────────────────────────────── */

static void print_usage(const char* prog)
{
    fprintf(stdout,
"Mcaster1 DSP Encoder v1.2.0 — Linux CLI\n"
"\n"
"Usage:\n"
"  %s [-d] [-v] -c <config>\n"
"  %s --ssl-gencert --ssl-gentype=selfsigned|csr\n"
"          --subj=\"/C=US/ST=.../CN=host\"\n"
"          --ssl-gencert-savepath=<dir>\n"
"          [--ssl-gencert-keysize=2048|4096]\n"
"          [--ssl-gencert-days=365]\n"
"          [--ssl-gencert-filename=server]\n"
"          [--ssl-gencert-addtoconfig=true] [-c <config>]\n"
"\n"
"Encoder options:\n"
"  -c, --config <file>        YAML config file\n"
"  -d, --daemon               Daemonize (run in background)\n"
"  -v, --verbose              Debug logging\n"
"  -w, --webroot <dir>        Web UI directory (default: ./web_ui)\n"
"\n"
"Admin HTTP options:\n"
"  --http-port  <port>        HTTP listener port  (default: 8330)\n"
"  --https-port <port>        HTTPS listener port (default: 8443)\n"
"  --bind       <addr>        Bind address        (default: 127.0.0.1)\n"
"  --ssl-cert   <path>        TLS certificate PEM\n"
"  --ssl-key    <path>        TLS private key PEM\n"
"  --admin-user <user>        Admin username      (default: admin)\n"
"  --admin-pass <pass>        Admin password      (default: hackme)\n"
"  --api-token  <token>       X-API-Token for script access\n"
"\n"
"SSL generation options:\n"
"  --ssl-gencert              Enter cert generation mode (exits after)\n"
"  --ssl-gentype=selfsigned|csr\n"
"  --subj=\"/C=.../CN=...\"     X.509 subject (openssl standard format)\n"
"  --ssl-gencert-savepath=<dir>\n"
"  --ssl-gencert-keysize=2048|4096    (default: 4096)\n"
"  --ssl-gencert-days=<n>             (default: 365, selfsigned only)\n"
"  --ssl-gencert-filename=<base>      (default: server)\n"
"  --ssl-gencert-addtoconfig=true     Patch -c config after generation\n"
"\n"
"  -h, --help                 Show this help\n"
"\n",
    prog, prog);
}

/* ── Long option table ────────────────────────────────────────────────────── */

static const struct option long_opts[] = {
    /* Encoder */
    { "config",                    required_argument, 0, 'c' },
    { "daemon",                    no_argument,       0, 'd' },
    { "verbose",                   no_argument,       0, 'v' },
    { "webroot",                   required_argument, 0, 'w' },
    /* Admin HTTP */
    { "http-port",                 required_argument, 0,  1  },
    { "https-port",                required_argument, 0,  2  },
    { "bind",                      required_argument, 0,  3  },
    { "ssl-cert",                  required_argument, 0,  4  },
    { "ssl-key",                   required_argument, 0,  5  },
    { "admin-user",                required_argument, 0,  6  },
    { "admin-pass",                required_argument, 0,  7  },
    { "api-token",                 required_argument, 0,  8  },
    /* SSL generation */
    { "ssl-gencert",               no_argument,       0, 'G' },
    { "ssl-gentype",               required_argument, 0,  10 },
    { "subj",                      required_argument, 0,  11 },
    { "ssl-gencert-savepath",      required_argument, 0,  12 },
    { "ssl-gencert-keysize",       required_argument, 0,  13 },
    { "ssl-gencert-days",          required_argument, 0,  14 },
    { "ssl-gencert-filename",      required_argument, 0,  15 },
    { "ssl-gencert-addtoconfig",   required_argument, 0,  16 },
    /* Help */
    { "help",                      no_argument,       0, 'h' },
    { 0, 0, 0, 0 }
};

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char* argv[])
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    admin_defaults();

    /* Defaults */
    const char* config_file  = nullptr;
    const char* webroot      = "./web_ui";
    bool        daemonize    = false;

    /* SSL gencert defaults */
    bool        do_gencert      = false;
    const char* gentype         = "selfsigned";
    const char* subj            = nullptr;
    const char* savepath        = ".";
    int         keysize         = 4096;
    int         cert_days       = 365;
    const char* cert_filename   = "server";
    bool        add_to_config   = false;

    /* HTTPS second socket defaults */
    int         https_port      = 8443;
    bool        want_https      = false;

    int opt, idx = 0;
    while ((opt = getopt_long(argc, argv, "c:dvw:Gh", long_opts, &idx)) != -1) {
        switch (opt) {
        /* Encoder */
        case 'c': config_file = optarg;                              break;
        case 'd': daemonize   = true;                                break;
        case 'v': /* TODO: setLogLevel(LM_DEBUG) */                  break;
        case 'w': webroot     = optarg;                              break;
        /* Admin HTTP */
        case  1:
            gAdminConfig.sockets[0].port = atoi(optarg);
            break;
        case  2:
            https_port  = atoi(optarg);
            want_https  = true;
            break;
        case  3:
            strncpy(gAdminConfig.sockets[0].bind_address, optarg,
                    sizeof(gAdminConfig.sockets[0].bind_address) - 1);
            break;
        case  4:
            strncpy(gAdminConfig.ssl_cert, optarg,
                    sizeof(gAdminConfig.ssl_cert) - 1);
            want_https = true;
            break;
        case  5:
            strncpy(gAdminConfig.ssl_key, optarg,
                    sizeof(gAdminConfig.ssl_key) - 1);
            break;
        case  6:
            strncpy(gAdminConfig.admin_username, optarg,
                    sizeof(gAdminConfig.admin_username) - 1);
            break;
        case  7:
            strncpy(gAdminConfig.admin_password, optarg,
                    sizeof(gAdminConfig.admin_password) - 1);
            break;
        case  8:
            strncpy(gAdminConfig.admin_api_token, optarg,
                    sizeof(gAdminConfig.admin_api_token) - 1);
            break;
        /* SSL gencert */
        case 'G': do_gencert    = true;                              break;
        case  10: gentype        = optarg;                           break;
        case  11: subj           = optarg;                           break;
        case  12: savepath       = optarg;                           break;
        case  13: keysize        = atoi(optarg);                     break;
        case  14: cert_days      = atoi(optarg);                     break;
        case  15: cert_filename  = optarg;                           break;
        case  16:
            add_to_config = (optarg && strcmp(optarg, "true") == 0);
            break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    /* ── SSL cert generation mode ────────────────────────────────────────── */
    if (do_gencert) {
        if (!subj) {
            fprintf(stderr,
                "[ssl] Error: --subj is required with --ssl-gencert\n"
                "      Example: --subj=\"/C=US/ST=FL/L=Miami/O=MyRadio/CN=radio.example.com\"\n");
            return 1;
        }
        return http_api_gencert(gentype, subj, savepath, keysize, cert_days,
                                cert_filename,
                                add_to_config ? config_file : nullptr);
    }

    /* ── Optional HTTPS second socket when cert+key are provided ─────────── */
    if (want_https && gAdminConfig.ssl_cert[0] && gAdminConfig.ssl_key[0]) {
        if (gAdminConfig.num_sockets < MC1_MAX_LISTENERS) {
            int i = gAdminConfig.num_sockets++;
            strncpy(gAdminConfig.sockets[i].bind_address, "0.0.0.0",
                    sizeof(gAdminConfig.sockets[i].bind_address) - 1);
            gAdminConfig.sockets[i].port        = https_port;
            gAdminConfig.sockets[i].ssl_enabled = 1;
        }
    }

    /* ── Daemonize ─────────────────────────────────────────────────────────- */
    if (daemonize) {
        if (daemon(0, 0) < 0) {
            perror("[encoder] daemon()");
            return 1;
        }
        fprintf(stdout, "[encoder] Running as daemon. PID %d\n", getpid());
    }

    /* ── Banner ─────────────────────────────────────────────────────────── */
    fprintf(stdout,
        "\n"
        "  Mcaster1 DSP Encoder v1.2.0 (Linux)\n"
        "  ──────────────────────────────────────\n");
    for (int i = 0; i < gAdminConfig.num_sockets; i++) {
        fprintf(stdout, "  Admin  : %s://%-15s:%d\n",
            gAdminConfig.sockets[i].ssl_enabled ? "https" : "http",
            gAdminConfig.sockets[i].bind_address,
            gAdminConfig.sockets[i].port);
    }
    fprintf(stdout, "  Webroot: %s\n", webroot);
    fprintf(stdout, "  Login  : %s / %s\n",
            gAdminConfig.admin_username, gAdminConfig.admin_password);
    fprintf(stdout, "  Press Ctrl+C to stop.\n\n");

    /* ── Initialize audio pipeline ───────────────────────────────────────── */
#ifndef MC1_HTTP_TEST_BUILD
    g_pipeline = new AudioPipeline();
#endif

    /* ── Start HTTP admin server (non-blocking) ──────────────────────────── */
    http_api_start(webroot);

    /* ── Block until signal ──────────────────────────────────────────────── */
    while (g_running) {
        sleep(1);
    }

#ifndef MC1_HTTP_TEST_BUILD
    delete g_pipeline;
    g_pipeline = nullptr;
#endif
    return 0;
}
