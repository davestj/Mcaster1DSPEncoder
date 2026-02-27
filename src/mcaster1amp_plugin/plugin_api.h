/*
 * Mcaster1AMP — Broadcaster-Grade Qt 6 Media Player
 * plugins/plugin_api.h — Public C ABI Plugin SDK v2
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  Mcaster1AMP Plugin SDK — Stable C ABI, API version 2
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * All plugins MUST export three C functions:
 *   amp_plugin_info()      → return static plugin metadata
 *   amp_plugin_init(ctx)   → initialize with host context (return 0 = OK)
 *   amp_plugin_shutdown()  → cleanup before dlclose
 *
 * Additional exports depending on plugin type (all optional unless noted):
 *
 *   DSP plugin (type 1):
 *     amp_plugin_dsp_process(samples, frames, channels)  [required]
 *     amp_plugin_set_param(name, value)                  [optional]
 *     amp_plugin_get_param(name) → double                [optional]
 *     amp_plugin_get_param_count() → int                 [optional]
 *     amp_plugin_get_param_info(index) → AmpParamInfo*   [optional]
 *     amp_plugin_create_ui(parent_qwidget*) → QWidget*   [optional]
 *     amp_plugin_destroy_ui(widget*)                      [optional]
 *
 *   Crossfader plugin (type 4):
 *     amp_plugin_crossfader_blend(a,b,out,frames,ch,pos) [required]
 *     amp_plugin_set_param / get_param / UI              [optional]
 *
 *   Input plugin (type 2):
 *     amp_plugin_input_can_decode(ext) → int             [required]
 *     amp_plugin_input_open(path, ...) → handle          [required]
 *     amp_plugin_input_read(handle, buf, frames) → int   [required]
 *     amp_plugin_input_close(handle)                     [required]
 *
 *   Visualization plugin (type 3):
 *     amp_plugin_vis_push(samples, frames, channels)     [required]
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  IMPORTANT: amp_plugin_create_ui / amp_plugin_destroy_ui are ONLY safe
 *  when the plugin and host share the same Qt runtime (same dylib/so).
 *  Third-party plugins that bundle Qt separately MUST NOT export these.
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AMP_PLUGIN_API_H
#define AMP_PLUGIN_API_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── API version ─────────────────────────────────────────────────── */

#define AMP_PLUGIN_API_VERSION 2

/* ── Plugin type constants ───────────────────────────────────────── */

#define AMP_PLUGIN_GENERAL       0   /* misc utility */
#define AMP_PLUGIN_DSP           1   /* in-place audio processing */
#define AMP_PLUGIN_INPUT         2   /* file decoder */
#define AMP_PLUGIN_VISUALIZATION 3   /* receive PCM for display */
#define AMP_PLUGIN_CROSSFADER    4   /* two-deck blend with curves */

/* ── Plugin metadata ────────────────────────────────────────────── */

typedef struct {
    const char* name;           /* "MC1 Crossfader"               */
    const char* author;         /* "Mcaster1 Audio"               */
    const char* description;    /* "Professional DJ crossfader"   */
    const char* version;        /* "1.0.0"                        */
    int         type;           /* AMP_PLUGIN_* constant          */
    int         api_version;    /* Must equal AMP_PLUGIN_API_VERSION */
} AmpPluginInfo;

/* ── Host context (passed to amp_plugin_init) ───────────────────── */

typedef void (*AmpLogCallback)(int level, const char* message);
/* Log levels: 1=CRITICAL, 2=ERROR, 3=WARNING, 4=INFO, 5=DEBUG */

typedef struct {
    int              sample_rate;
    int              channels;
    int              buffer_size;     /* frames per process call   */
    const char*      app_version;     /* e.g. "1.0.0"             */
    const char*      plugin_data_dir; /* writable per-plugin dir  */
    AmpLogCallback   log;             /* host logging callback     */
} AmpHostContext;

/* ── Required exports (ALL plugin types) ────────────────────────── */

typedef const AmpPluginInfo* (*AmpPluginInfoFn)(void);
typedef int                  (*AmpPluginInitFn)(const AmpHostContext*);
typedef void                 (*AmpPluginShutdownFn)(void);

/* ── DSP plugin: core audio callback ────────────────────────────── */
/*
 * Process audio in-place. samples is interleaved float PCM [-1,1].
 * frames = number of sample frames; channels = 1 (mono) or 2 (stereo).
 * MUST be realtime-safe: no alloc, no lock, no IO, no blocking.
 */
typedef void (*AmpPluginDspProcessFn)(float* samples, int frames, int channels);

/* ── Parameter API (optional for DSP and Crossfader plugins) ─────── */

/*
 * Set a named parameter. name is a null-terminated ASCII string.
 * Plugin may silently ignore unknown names.
 */
typedef void   (*AmpPluginSetParamFn)(const char* name, double value);

/*
 * Get a named parameter value. Returns 0.0 for unknown names.
 */
typedef double (*AmpPluginGetParamFn)(const char* name);

/*
 * Parameter descriptor — returned by amp_plugin_get_param_info().
 * Used by the host to build generic parameter UI if the plugin
 * doesn't provide a custom widget via amp_plugin_create_ui().
 */
typedef struct {
    const char* name;          /* internal key used in set/get_param */
    const char* label;         /* human-readable display name         */
    const char* unit;          /* "dB", "%", "Hz", "" etc.           */
    double      min_value;
    double      max_value;
    double      default_value;
    int         is_enum;       /* 1 = integer index into enum_names   */
    const char* enum_names;    /* pipe-separated: "Linear|CP|S-Curve" */
} AmpParamInfo;

typedef int                  (*AmpPluginGetParamCountFn)(void);
typedef const AmpParamInfo*  (*AmpPluginGetParamInfoFn)(int param_index);

/* ── Custom UI API (optional — Qt shared runtime only) ───────────── */

/*
 * Create and return a QWidget* for this plugin's settings UI.
 * parent_qwidget: host passes QWidget* cast to void*.
 * Plugin returns new QWidget* cast to void*, or NULL if no UI.
 * Host takes ownership and will call amp_plugin_destroy_ui() before unload.
 */
typedef void* (*AmpPluginCreateUiFn)(void* parent_qwidget);

/*
 * Destroy the widget returned by amp_plugin_create_ui().
 * ui_widget is the same void* returned by create_ui.
 */
typedef void  (*AmpPluginDestroyUiFn)(void* ui_widget);

/* ── Crossfader plugin: two-deck blend callback ──────────────────── */

/*
 * Blend Deck A and Deck B into output.
 * deck_a, deck_b: interleaved float PCM [-1,1] (read-only)
 * out:            interleaved float PCM output (write)
 * position:       0.0=full A, 0.5=equal mix, 1.0=full B
 * Realtime-safe: no alloc, no lock, no IO.
 */
typedef void (*AmpPluginCrossfaderBlendFn)(
    const float* deck_a, const float* deck_b,
    float* out, int frames, int channels, float position);

/* ── Input plugin callbacks ─────────────────────────────────────── */

typedef int   (*AmpPluginInputCanDecodeFn)(const char* ext);
typedef void* (*AmpPluginInputOpenFn)(const char* path,
                                      int* out_sample_rate, int* out_channels);
typedef int   (*AmpPluginInputReadFn)(void* handle, float* buffer, int max_frames);
typedef void  (*AmpPluginInputCloseFn)(void* handle);

/* ── Visualization plugin callback ──────────────────────────────── */

typedef void (*AmpPluginVisPushFn)(const float* samples, int frames, int channels);

/* ── Symbol name constants (use these with dlsym) ───────────────── */

/* Required (all types) */
#define AMP_SYM_INFO            "amp_plugin_info"
#define AMP_SYM_INIT            "amp_plugin_init"
#define AMP_SYM_SHUTDOWN        "amp_plugin_shutdown"

/* DSP */
#define AMP_SYM_DSP_PROCESS     "amp_plugin_dsp_process"

/* Parameter API */
#define AMP_SYM_SET_PARAM       "amp_plugin_set_param"
#define AMP_SYM_GET_PARAM       "amp_plugin_get_param"
#define AMP_SYM_GET_PARAM_COUNT "amp_plugin_get_param_count"
#define AMP_SYM_GET_PARAM_INFO  "amp_plugin_get_param_info"

/* Custom UI (Qt shared runtime) */
#define AMP_SYM_CREATE_UI       "amp_plugin_create_ui"
#define AMP_SYM_DESTROY_UI      "amp_plugin_destroy_ui"

/* Crossfader */
#define AMP_SYM_CROSSFADER_BLEND "amp_plugin_crossfader_blend"

/* Input */
#define AMP_SYM_INPUT_CAN       "amp_plugin_input_can_decode"
#define AMP_SYM_INPUT_OPEN      "amp_plugin_input_open"
#define AMP_SYM_INPUT_READ      "amp_plugin_input_read"
#define AMP_SYM_INPUT_CLOSE     "amp_plugin_input_close"

/* Visualization */
#define AMP_SYM_VIS_PUSH        "amp_plugin_vis_push"

#ifdef __cplusplus
}
#endif

#endif /* AMP_PLUGIN_API_H */
