// config_stub.cpp — Minimal global definitions for HTTP browser test build.
//
// Provides gAdminConfig without pulling in libyaml or the full encoder core.
// Used only by make_http_test.sh.  Full builds use config_yaml.cpp instead.

#include "../platform.h"
#include "../libmcaster1dspencoder/libmcaster1dspencoder.h"
#include <string.h>

mc1AdminConfig gAdminConfig;
