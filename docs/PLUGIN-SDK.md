# Mcaster1 DSP Plugin SDK v1.0

Third-party developers can create DSP effect plugins that load into the Mcaster1 effects rack at runtime. Plugins are shared libraries (`.so`) that implement a standard C interface.

## Quick Start

1. Include `src/linux/dsp/mc1_plugin_api.h` in your plugin source
2. Implement all required C functions (see API Reference below)
3. Build: `g++ -shared -fPIC -O2 -o my_plugin.so my_plugin.cpp`
4. Install to one of the plugin search directories
5. Restart mcaster1-dsp-encoder or click "Scan for Plugins" in Settings

## Plugin Search Directories

The plugin loader scans these directories in order:

| Directory | Purpose |
|-----------|---------|
| `/usr/lib/mcaster1/plugins/` | System-wide installation |
| `/usr/local/lib/mcaster1/plugins/` | Local system installation |
| `~/.mcaster1/plugins/` | Per-user plugins |
| `plugins/` | Project-local (development) |

## API Reference

Every plugin `.so` must export all of the following C functions. Missing any symbol causes the plugin to be rejected at load time.

### mc1_plugin_info

```c
mc1_plugin_info_t mc1_plugin_info(void);
```

Returns plugin metadata. Called once when the plugin is loaded.

**Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `api_version` | `int` | Must be `MC1_PLUGIN_API_VERSION` (currently 1) |
| `type_id` | `const char*` | Unique identifier, e.g. `"vendor.effect_name"` |
| `display_name` | `const char*` | Human-readable name, e.g. `"My Reverb"` |
| `version` | `const char*` | Semantic version, e.g. `"1.0.0"` |
| `author` | `const char*` | Developer name |
| `description` | `const char*` | Short description of the effect |
| `num_params` | `int` | Number of configurable parameters |

### mc1_plugin_params

```c
mc1_param_desc_t* mc1_plugin_params(int* count);
```

Returns a pointer to an array of parameter descriptors and sets `*count` to the array size. The returned pointer must remain valid for the lifetime of the `.so`.

**Parameter descriptor fields:**
| Field | Type | Description |
|-------|------|-------------|
| `key` | `const char*` | Parameter key for get/set, e.g. `"mix"` |
| `label` | `const char*` | Display label, e.g. `"Wet/Dry Mix"` |
| `default_val` | `float` | Default value |
| `min_val` | `float` | Minimum value |
| `max_val` | `float` | Maximum value |
| `step` | `float` | UI slider step size, 0 = continuous |
| `unit` | `const char*` | Unit string: `"dB"`, `"ms"`, `"%"`, `""` |

### mc1_plugin_create

```c
mc1_plugin_handle_t mc1_plugin_create(int sample_rate, int channels);
```

Creates a new plugin instance for the given sample rate and channel count. Returns an opaque handle, or `NULL` on failure. Multiple instances may be created (one per effects rack).

### mc1_plugin_destroy

```c
void mc1_plugin_destroy(mc1_plugin_handle_t h);
```

Destroys a plugin instance and frees all its resources.

### mc1_plugin_process

```c
void mc1_plugin_process(mc1_plugin_handle_t h, float* pcm, size_t frames, int channels);
```

Processes audio in-place. `pcm` is interleaved float data in the range [-1.0, 1.0]. `frames` is the number of sample frames, `channels` is the interleave factor (1 = mono, 2 = stereo).

**CRITICAL: This function is called on the real-time audio thread.** It must be:
- Lock-free (no mutexes, no condition variables)
- Allocation-free (no `malloc`, `new`, or STL container resizing)
- I/O-free (no file reads, no network, no logging)
- Bounded execution time (no unbounded loops)

### mc1_plugin_set_param

```c
void mc1_plugin_set_param(mc1_plugin_handle_t h, const char* key, float value);
```

Sets a parameter by key. Ignored if the key is unknown. Called from the HTTP API thread (not the audio thread).

### mc1_plugin_get_param

```c
float mc1_plugin_get_param(mc1_plugin_handle_t h, const char* key);
```

Gets the current value of a parameter by key. Returns 0 if the key is unknown.

### mc1_plugin_reset

```c
void mc1_plugin_reset(mc1_plugin_handle_t h);
```

Resets the plugin to its initial state. Clears delay lines, envelopes, and any accumulated state.

## Example Plugin

See `plugins/example_gain.cpp` for a complete working example. It implements a simple gain effect with one parameter.

### Building the Example

```bash
cd /var/www/mcaster1.com/Mcaster1DSPEncoder
g++ -shared -fPIC -O2 -o plugins/example_gain.so plugins/example_gain.cpp
```

### Example Plugin Structure

```cpp
#include "src/linux/dsp/mc1_plugin_api.h"
#include <cstring>

// Instance state
struct MyState {
    float param1;
    int   sample_rate;
    int   channels;
};

// Static parameter descriptors
static mc1_param_desc_t s_params[] = {
    { "param1", "Parameter 1", 0.5f, 0.0f, 1.0f, 0.01f, "" }
};

extern "C" {

mc1_plugin_info_t mc1_plugin_info(void) {
    mc1_plugin_info_t info;
    info.api_version  = MC1_PLUGIN_API_VERSION;
    info.type_id      = "mycompany.my_effect";
    info.display_name = "My Effect";
    info.version      = "1.0.0";
    info.author       = "My Name";
    info.description  = "Does something cool to audio";
    info.num_params   = 1;
    return info;
}

mc1_param_desc_t* mc1_plugin_params(int* count) {
    *count = 1;
    return s_params;
}

mc1_plugin_handle_t mc1_plugin_create(int sample_rate, int channels) {
    MyState* s = new MyState();
    s->param1 = 0.5f;
    s->sample_rate = sample_rate;
    s->channels = channels;
    return s;
}

void mc1_plugin_destroy(mc1_plugin_handle_t h) {
    delete static_cast<MyState*>(h);
}

void mc1_plugin_process(mc1_plugin_handle_t h, float* pcm,
                        size_t frames, int channels) {
    MyState* s = static_cast<MyState*>(h);
    size_t n = frames * channels;
    for (size_t i = 0; i < n; ++i) {
        pcm[i] *= s->param1;  // Your DSP here
    }
}

void mc1_plugin_set_param(mc1_plugin_handle_t h, const char* key, float value) {
    MyState* s = static_cast<MyState*>(h);
    if (strcmp(key, "param1") == 0) s->param1 = value;
}

float mc1_plugin_get_param(mc1_plugin_handle_t h, const char* key) {
    MyState* s = static_cast<MyState*>(h);
    if (strcmp(key, "param1") == 0) return s->param1;
    return 0.0f;
}

void mc1_plugin_reset(mc1_plugin_handle_t h) {
    MyState* s = static_cast<MyState*>(h);
    s->param1 = 0.5f;
}

} // extern "C"
```

## HTTP API

### List Installed Plugins

```
GET /api/v1/plugins
```

Returns all loaded plugins with metadata, parameter counts, paths, and enabled status.

### Scan for Plugins

```
POST /api/v1/plugins/scan
```

Rescans all plugin directories and loads any new `.so` files found. Returns the count of newly loaded plugins.

### Get Plugin Parameters

```
GET /api/v1/plugins/{type_id}/params
```

Returns the full parameter descriptor array for a plugin, including key, label, min/max/default values, step, and unit.

### Enable/Disable Plugin

```
PUT /api/v1/plugins/{type_id}/enabled
Body: { "enabled": true }
```

Enables or disables a plugin. Disabled plugins are not available in the effects rack but remain loaded.

## Using Plugins in the Effects Rack

Once loaded, plugins appear in the effects rack "Add Unit" dropdown alongside built-in effects. They can be:

- Added to any effects rack (global or per-encoder)
- Reordered in the chain like any built-in effect
- Enabled/disabled per-instance
- Configured via the standard parameter API

### Adding a Plugin Unit via API

```
POST /api/v1/effects/global/units
{ "type": "vendor.effect_name" }
```

### Setting Plugin Parameters via API

```
PUT /api/v1/effects/global
{
  "units": [{
    "id": 5,
    "params": { "gain": 1.5, "mix": 0.7 }
  }]
}
```

## Plugin Development Guidelines

1. **Use `extern "C"`** for all exported functions to prevent C++ name mangling
2. **Use a unique `type_id`** in `vendor.name` format to avoid collisions
3. **Keep `process()` realtime-safe** -- no allocations, locks, or I/O
4. **Validate all inputs** -- check for NULL handles and pointers
5. **Clamp parameter values** in `set_param()` to the declared min/max range
6. **Do not call `exit()`** -- this would terminate the entire encoder process
7. **Handle sample rate changes** -- `mc1_plugin_create` may be called with different rates
8. **Thread safety** -- `set_param`/`get_param` may be called from a different thread than `process()`. Use atomics for parameters read by the audio thread.

## ABI Compatibility

The plugin API uses a C interface for maximum ABI compatibility. Plugins compiled with any C++ compiler (GCC, Clang) will work as long as they export the required C symbols.

The `api_version` field in `mc1_plugin_info_t` is checked at load time. Plugins compiled against a different API version will be rejected with an error message in the log.

## Debugging

Plugin load errors are logged to `/var/log/mcaster1/error.log`:

```
[WARN] Plugin /path/to/bad.so: missing mc1_plugin_info
[WARN] Plugin /path/to/old.so: API version mismatch (got 0, expected 1)
[INFO] Loaded plugin: Example Gain v1.0.0 (example.gain) from plugins/example_gain.so
```

Enable debug logging (`--log-level 5` or `-v`) to see plugin directory scanning output.
