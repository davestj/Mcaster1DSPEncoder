/*
 * Mcaster1 DSP Plugin SDK — Example Gain Plugin
 * plugins/example_gain.cpp
 *
 * A minimal plugin that demonstrates the Mcaster1 Plugin API.
 * Applies a configurable gain factor to all audio samples.
 *
 * Build:
 *   g++ -shared -fPIC -O2 -o example_gain.so example_gain.cpp
 *
 * Install:
 *   cp example_gain.so /usr/lib/mcaster1/plugins/
 *   # or: cp example_gain.so ~/.mcaster1/plugins/
 *   # or: leave in the project plugins/ directory for dev mode
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../src/linux/dsp/mc1_plugin_api.h"

#include <cstring>
#include <cmath>

/* ══════════════════════════════════════════════════════════════════════════════
 * Plugin instance state
 * ══════════════════════════════════════════════════════════════════════════════ */
struct GainState {
    float gain;          /* Linear gain factor 0.0 - 2.0 */
    int   sample_rate;
    int   channels;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * Parameter descriptors (static — lifetime of the .so)
 * ══════════════════════════════════════════════════════════════════════════════ */
static mc1_param_desc_t s_params[] = {
    {
        /* key         */ "gain",
        /* label       */ "Gain",
        /* default_val */ 1.0f,
        /* min_val     */ 0.0f,
        /* max_val     */ 2.0f,
        /* step        */ 0.01f,
        /* unit        */ "x"
    }
};

static const int s_param_count = sizeof(s_params) / sizeof(s_params[0]);

/* ══════════════════════════════════════════════════════════════════════════════
 * Required API exports
 * ══════════════════════════════════════════════════════════════════════════════ */

extern "C" {

mc1_plugin_info_t mc1_plugin_info(void) {
    mc1_plugin_info_t info;
    info.api_version  = MC1_PLUGIN_API_VERSION;
    info.type_id      = "example.gain";
    info.display_name = "Example Gain";
    info.version      = "1.0.0";
    info.author       = "Mcaster1 SDK";
    info.description  = "Simple gain plugin that scales audio by a configurable factor. "
                        "Demonstrates the Mcaster1 Plugin API.";
    info.num_params   = s_param_count;
    return info;
}

mc1_param_desc_t* mc1_plugin_params(int* count) {
    if (count) *count = s_param_count;
    return s_params;
}

mc1_plugin_handle_t mc1_plugin_create(int sample_rate, int channels) {
    GainState* state = new GainState();
    state->gain        = 1.0f;
    state->sample_rate = sample_rate;
    state->channels    = channels;
    return static_cast<mc1_plugin_handle_t>(state);
}

void mc1_plugin_destroy(mc1_plugin_handle_t h) {
    delete static_cast<GainState*>(h);
}

void mc1_plugin_process(mc1_plugin_handle_t h, float* pcm,
                        size_t frames, int channels) {
    GainState* state = static_cast<GainState*>(h);
    if (!state || !pcm) return;

    const float g = state->gain;
    const size_t n = frames * static_cast<size_t>(channels);
    for (size_t i = 0; i < n; ++i) {
        pcm[i] *= g;
    }
}

void mc1_plugin_set_param(mc1_plugin_handle_t h, const char* key, float value) {
    GainState* state = static_cast<GainState*>(h);
    if (!state || !key) return;

    if (strcmp(key, "gain") == 0) {
        /* We clamp to valid range */
        if (value < 0.0f) value = 0.0f;
        if (value > 2.0f) value = 2.0f;
        state->gain = value;
    }
}

float mc1_plugin_get_param(mc1_plugin_handle_t h, const char* key) {
    GainState* state = static_cast<GainState*>(h);
    if (!state || !key) return 0.0f;

    if (strcmp(key, "gain") == 0) return state->gain;
    return 0.0f;
}

void mc1_plugin_reset(mc1_plugin_handle_t h) {
    GainState* state = static_cast<GainState*>(h);
    if (!state) return;
    state->gain = 1.0f;
}

} /* extern "C" */
