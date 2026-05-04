/*
 * Mcaster1 DSP Plugin API v1.0
 * dsp/mc1_plugin_api.h
 *
 * Third-party plugins implement this interface to create DSP effects
 * that load into the Mcaster1 effects rack at runtime.
 *
 * Compile as: g++ -shared -fPIC -o my_effect.so my_effect.cpp
 * Install to: /usr/lib/mcaster1/plugins/ or ~/.mcaster1/plugins/
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_PLUGIN_API_H
#define MC1_PLUGIN_API_H

#include <stdint.h>
#include <stddef.h>

#define MC1_PLUGIN_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════════════════
 * Plugin metadata — returned by mc1_plugin_info()
 * ══════════════════════════════════════════════════════════════════════════════ */
typedef struct {
    int         api_version;      /* Must be MC1_PLUGIN_API_VERSION */
    const char* type_id;          /* Unique: "vendor.effect_name" */
    const char* display_name;     /* "My Awesome Reverb" */
    const char* version;          /* "1.0.0" */
    const char* author;           /* "Developer Name" */
    const char* description;      /* Short description */
    int         num_params;       /* Number of configurable parameters */
} mc1_plugin_info_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * Parameter descriptor — describes one tunable knob/slider
 * ══════════════════════════════════════════════════════════════════════════════ */
typedef struct {
    const char* key;              /* "mix", "decay", "threshold" */
    const char* label;            /* "Wet/Dry Mix" */
    float       default_val;
    float       min_val;
    float       max_val;
    float       step;             /* UI slider step, 0 = continuous */
    const char* unit;             /* "dB", "ms", "%", "" */
} mc1_param_desc_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * Plugin instance handle — opaque pointer to plugin-managed state
 * ══════════════════════════════════════════════════════════════════════════════ */
typedef void* mc1_plugin_handle_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * Required exports from the .so
 *
 * Every plugin shared library MUST export all of these symbols.
 * The loader resolves them via dlsym and rejects plugins missing any.
 *
 * IMPORTANT: process() is called on the real-time audio thread.
 * It must be lock-free and must not allocate memory, block on I/O,
 * or call any non-realtime-safe functions.
 * ══════════════════════════════════════════════════════════════════════════════ */

/* We return plugin metadata. Called once at load time. */
mc1_plugin_info_t       mc1_plugin_info(void);

/* We return an array of parameter descriptors and set *count to the array size.
 * The returned pointer must remain valid for the lifetime of the .so. */
mc1_param_desc_t*       mc1_plugin_params(int* count);

/* We create a new plugin instance for the given sample rate and channel count.
 * Returns an opaque handle, or NULL on failure. */
mc1_plugin_handle_t     mc1_plugin_create(int sample_rate, int channels);

/* We destroy a plugin instance and free all its resources. */
void                    mc1_plugin_destroy(mc1_plugin_handle_t h);

/* We process audio in-place. pcm is interleaved float [-1.0, 1.0].
 * frames = number of sample frames, channels = interleave factor.
 * MUST be realtime-safe: no malloc, no locks, no I/O. */
void                    mc1_plugin_process(mc1_plugin_handle_t h,
                                           float* pcm, size_t frames,
                                           int channels);

/* We set a parameter by key. Ignored if key is unknown. */
void                    mc1_plugin_set_param(mc1_plugin_handle_t h,
                                             const char* key, float value);

/* We get the current value of a parameter by key. Returns 0 if key unknown. */
float                   mc1_plugin_get_param(mc1_plugin_handle_t h,
                                             const char* key);

/* We reset the plugin to its initial state (clear delay lines, envelopes, etc). */
void                    mc1_plugin_reset(mc1_plugin_handle_t h);

#ifdef __cplusplus
}
#endif

#endif /* MC1_PLUGIN_API_H */
