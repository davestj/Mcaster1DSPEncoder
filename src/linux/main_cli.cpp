// main_cli.cpp — mcaster1-encoder CLI entry point (Linux)
//
// Precedence (highest wins):
//   1. CLI flags (--bind, --http-port, --admin-user, --admin-pass, --webroot, …)
//   2. YAML config file (-c / --config)
//   3. Compiled-in defaults
//
// Workflow:
//   1. Set hard defaults in gAdminConfig
//   2. Parse CLI flags into local "cli_*" variables; record which were set
//   3. If -c was given: mc1_load_config() → fills gAdminConfig + pipeline slots
//   4. Re-apply any explicitly-set CLI flags on top of YAML values
//   5. Start HTTP/HTTPS admin server

#include "../platform.h"
#include "../libmcaster1dspencoder/libmcaster1dspencoder.h"
#include "http_api.h"
#include "config_loader.h"
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
#include <string>
#include <exception>
#include <execinfo.h>

/* ── Terminate handler — prints backtrace on std::terminate ───────────────── */

static void mc1_terminate_handler()
{
    // We print the exception info, then a backtrace, then abort
    try {
        std::rethrow_exception(std::current_exception());
    } catch (const std::exception& e) {
        fprintf(stderr, "\n[mc1 FATAL] std::terminate — %s: %s\n",
                typeid(e).name(), e.what());
    } catch (...) {
        fprintf(stderr, "\n[mc1 FATAL] std::terminate — unknown exception\n");
    }

    // Stack trace via execinfo
    void* frames[64];
    int   n = backtrace(frames, 64);
    char** syms = backtrace_symbols(frames, n);
    fprintf(stderr, "[mc1 BACKTRACE] %d frames:\n", n);
    for (int i = 0; i < n; i++)
        fprintf(stderr, "  #%02d  %s\n", i, syms ? syms[i] : "?");
    if (syms) free(syms);
    fflush(stderr);

    // Default behaviour
    abort();
}

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

    strncpy(gAdminConfig.sockets[0].bind_address, "0.0.0.0",
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
"Mcaster1 DSP Encoder v1.3.0 — Linux CLI\n"
"\n"
"Usage:\n"
"  %s -c <config.yaml>                    # recommended: full YAML config\n"
"  %s --http-port 8330 --bind 0.0.0.0    # CLI-only (no encoder slots)\n"
"  %s --ssl-gencert --ssl-gentype=selfsigned\n"
"          --subj=\"/C=US/ST=.../CN=host\"\n"
"          --ssl-gencert-savepath=<dir>\n"
"\n"
"Encoder options:\n"
"  -c, --config <file>        YAML config file (sets sockets, creds, slots)\n"
"  -d, --daemon               Daemonize (run in background)\n"
"  -v, --verbose              Debug logging\n"
"  -w, --webroot <dir>        Web UI directory (overrides YAML webroot)\n"
"\n"
"Admin HTTP options (override YAML values when specified):\n"
"  --http-port  <port>        HTTP listener port  (default: 8330)\n"
"  --https-port <port>        Add HTTPS socket on this port\n"
"  --bind       <addr>        Bind address for socket[0] (default: 0.0.0.0)\n"
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
    prog, prog, prog);
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
    { "log-level",                 required_argument, 0,  17 },
    { "log-dir",                   required_argument, 0,  18 },
    /* Help */
    { "help",                      no_argument,       0, 'h' },
    { 0, 0, 0, 0 }
};

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char* argv[])
{
    std::set_terminate(mc1_terminate_handler);
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* Step 1: Set hard defaults */
    admin_defaults();

    /* Step 2: Collect CLI flags (record which were explicitly set) */
    const char* config_file  = nullptr;
    bool        daemonize    = false;

    // CLI override tracking — only applied after YAML load
    bool        cli_bind_set      = false;  std::string cli_bind;
    bool        cli_http_port_set = false;  int         cli_http_port = 8330;
    bool        cli_user_set      = false;  std::string cli_user;
    bool        cli_pass_set      = false;  std::string cli_pass;
    bool        cli_token_set     = false;  std::string cli_token;
    bool        cli_webroot_set   = false;  std::string cli_webroot = "./web_ui";
    bool        cli_ssl_cert_set  = false;  std::string cli_ssl_cert;
    bool        cli_ssl_key_set   = false;  std::string cli_ssl_key;
    bool        cli_https_port_set= false;  int         cli_https_port = 8344;
    bool        cli_loglevel_set  = false;  int         cli_loglevel = 4;
    bool        cli_logdir_set    = false;  std::string cli_logdir;

    // SSL gencert options
    bool        do_gencert      = false;
    const char* gentype         = "selfsigned";
    const char* subj            = nullptr;
    const char* savepath        = ".";
    int         keysize         = 4096;
    int         cert_days       = 365;
    const char* cert_filename   = "server";
    bool        add_to_config   = false;

    int opt, idx = 0;
    while ((opt = getopt_long(argc, argv, "c:dvw:Gh", long_opts, &idx)) != -1) {
        switch (opt) {
        case 'c': config_file = optarg;                              break;
        case 'd': daemonize   = true;                                break;
        case 'v': cli_loglevel_set = true; cli_loglevel = 5;          break;
        case 'w': cli_webroot_set = true;  cli_webroot  = optarg;   break;
        /* Admin HTTP */
        case  1: cli_http_port_set  = true; cli_http_port  = atoi(optarg); break;
        case  2: cli_https_port_set = true; cli_https_port = atoi(optarg); break;
        case  3: cli_bind_set       = true; cli_bind       = optarg;       break;
        case  4: cli_ssl_cert_set   = true; cli_ssl_cert   = optarg;       break;
        case  5: cli_ssl_key_set    = true; cli_ssl_key    = optarg;       break;
        case  6: cli_user_set       = true; cli_user       = optarg;       break;
        case  7: cli_pass_set       = true; cli_pass       = optarg;       break;
        case  8: cli_token_set      = true; cli_token      = optarg;       break;
        case  17: cli_loglevel_set  = true; cli_loglevel  = atoi(optarg); break;
        case  18: cli_logdir_set    = true; cli_logdir    = optarg;       break;
        /* SSL gencert */
        case 'G': do_gencert    = true;                              break;
        case  10: gentype        = optarg;                           break;
        case  11: subj           = optarg;                           break;
        case  12: savepath       = optarg;                           break;
        case  13: keysize        = atoi(optarg);                     break;
        case  14: cert_days      = atoi(optarg);                     break;
        case  15: cert_filename  = optarg;                           break;
        case  16: add_to_config  = (optarg && strcmp(optarg,"true")==0); break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    /* SSL cert generation mode — exits immediately */
    if (do_gencert) {
        if (!subj) {
            fprintf(stderr,
                "[ssl] Error: --subj is required with --ssl-gencert\n"
                "      Example: --subj=\"/C=US/ST=FL/L=Miami/O=MyRadio/CN=encoder.mcaster1.com\"\n");
            return 1;
        }
        return http_api_gencert(gentype, subj, savepath, keysize, cert_days,
                                cert_filename,
                                add_to_config ? config_file : nullptr);
    }

    /* Step 3: Initialise audio pipeline */
#ifndef MC1_HTTP_TEST_BUILD
    g_pipeline = new AudioPipeline();
#endif

    /* Step 4: Load YAML startup config — fills gAdminConfig (HTTP, DB, DNAS, log).
     *          Encoder slots are NOT loaded from YAML; they come from MySQL below. */
    std::string webroot_from_yaml;
    if (config_file) {
#ifndef MC1_HTTP_TEST_BUILD
        if (!mc1_load_config(config_file, g_pipeline, &webroot_from_yaml)) {
            fprintf(stderr, "[encoder] Fatal: config load failed: %s\n", config_file);
            delete g_pipeline;
            return 1;
        }
#else
        mc1_load_config(config_file, nullptr, &webroot_from_yaml);
#endif
        // Use webroot from YAML if CLI didn't override it
        if (!cli_webroot_set && !webroot_from_yaml.empty())
            cli_webroot = webroot_from_yaml;
    }

    /* Step 4b: Load encoder slots from MySQL encoder_configs table.
     *          We do this after YAML so DB credentials are available. */
#ifndef MC1_HTTP_TEST_BUILD
    if (g_pipeline) {
        if (!mc1_load_slots_from_db(g_pipeline)) {
            fprintf(stderr, "[encoder] Warning: no active encoder slots loaded from DB"
                            " — check encoder_configs table and DB credentials in YAML\n");
            // We continue anyway — slots can be added via the web UI
        } else {
            // Connect Mc1Db + start SystemHealth + ServerMonitor background threads.
            g_pipeline->start_background_services();
            // We start any slots that have auto_start=true in their DB config.
            // Each slot fires in its own detached thread after its configured delay.
            g_pipeline->start_auto_slots();
        }
    }
#endif

    /* Step 5: Re-apply CLI overrides on top of YAML values */
    if (cli_bind_set)
        strncpy(gAdminConfig.sockets[0].bind_address, cli_bind.c_str(),
                sizeof(gAdminConfig.sockets[0].bind_address) - 1);

    if (cli_http_port_set)
        gAdminConfig.sockets[0].port = cli_http_port;

    if (cli_user_set)
        strncpy(gAdminConfig.admin_username, cli_user.c_str(),
                sizeof(gAdminConfig.admin_username) - 1);

    if (cli_pass_set)
        strncpy(gAdminConfig.admin_password, cli_pass.c_str(),
                sizeof(gAdminConfig.admin_password) - 1);

    if (cli_token_set)
        strncpy(gAdminConfig.admin_api_token, cli_token.c_str(),
                sizeof(gAdminConfig.admin_api_token) - 1);

    if (cli_loglevel_set)
        gAdminConfig.log_level = cli_loglevel;

    if (cli_logdir_set)
        strncpy(gAdminConfig.log_dir, cli_logdir.c_str(),
                sizeof(gAdminConfig.log_dir) - 1);

    // Add an extra HTTPS socket if --https-port was given on CLI
    // (only if YAML didn't already configure an SSL socket on that port)
    if (cli_https_port_set && gAdminConfig.num_sockets < MC1_MAX_LISTENERS) {
        // Check YAML didn't already add this port
        bool already_have = false;
        for (int i = 0; i < gAdminConfig.num_sockets; i++) {
            if (gAdminConfig.sockets[i].port == cli_https_port &&
                gAdminConfig.sockets[i].ssl_enabled) {
                already_have = true;
                break;
            }
        }
        if (!already_have) {
            int i = gAdminConfig.num_sockets++;
            strncpy(gAdminConfig.sockets[i].bind_address, "0.0.0.0",
                    sizeof(gAdminConfig.sockets[i].bind_address) - 1);
            gAdminConfig.sockets[i].port        = cli_https_port;
            gAdminConfig.sockets[i].ssl_enabled = 1;

            if (cli_ssl_cert_set)
                strncpy(gAdminConfig.sockets[i].ssl_cert, cli_ssl_cert.c_str(),
                        sizeof(gAdminConfig.sockets[i].ssl_cert) - 1);
            if (cli_ssl_key_set)
                strncpy(gAdminConfig.sockets[i].ssl_key, cli_ssl_key.c_str(),
                        sizeof(gAdminConfig.sockets[i].ssl_key) - 1);
        }
    }

    // Global SSL cert/key override (used if per-socket fields are empty)
    if (cli_ssl_cert_set)
        strncpy(gAdminConfig.ssl_cert, cli_ssl_cert.c_str(),
                sizeof(gAdminConfig.ssl_cert) - 1);
    if (cli_ssl_key_set)
        strncpy(gAdminConfig.ssl_key, cli_ssl_key.c_str(),
                sizeof(gAdminConfig.ssl_key) - 1);

    /* Step 6: Daemonize (if requested) */
    if (daemonize) {
        if (daemon(0, 0) < 0) {
            perror("[encoder] daemon()");
            return 1;
        }
        fprintf(stdout, "[encoder] Running as daemon. PID %d\n", getpid());
    }

    /* Step 7: Print startup banner */
    fprintf(stdout,
        "\n"
        "  Mcaster1 DSP Encoder v1.3.0 (Linux)\n"
        "  ──────────────────────────────────────────────────────\n");
    for (int i = 0; i < gAdminConfig.num_sockets; i++) {
        const mc1ListenSocket& s = gAdminConfig.sockets[i];
        fprintf(stdout, "  Admin  : %s://%-15s:%d\n",
            s.ssl_enabled ? "https" : "http",
            s.bind_address, s.port);
    }
    fprintf(stdout, "  Webroot: %s\n", cli_webroot.c_str());
    fprintf(stdout, "  Login  : %s / %s\n",
            gAdminConfig.admin_username, gAdminConfig.admin_password);
#ifndef MC1_HTTP_TEST_BUILD
    fprintf(stdout, "  Slots  : %d encoder(s) configured\n",
            g_pipeline ? g_pipeline->slot_count() : 0);
#endif
    fprintf(stdout, "  Host   : encoder.mcaster1.com\n");
    fprintf(stdout, "  Press Ctrl+C to stop.\n\n");

    /* Step 8: Start HTTP/HTTPS admin server (non-blocking) */
    http_api_start(cli_webroot);

    /* Step 9: Block until SIGINT / SIGTERM */
    while (g_running) {
        sleep(1);
    }

#ifndef MC1_HTTP_TEST_BUILD
    delete g_pipeline;
    g_pipeline = nullptr;
#endif
    return 0;
}
