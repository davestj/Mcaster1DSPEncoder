// config_yaml.cpp — YAML config implementation for Mcaster1DSPEncoder
//
// Uses the raw libyaml C API (vcpkg libyaml:x86-windows).
// File format: flat YAML mapping — one key: value per encoder setting.
// Key names are identical to the legacy INI system so that configAddKeyValue()
// + config_read() fire callbacks exactly as the INI path does.
//

#include "stdafx.h"
#include "config_yaml.h"
#include <yaml.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

// Build the YAML file path from g->gConfigFileName and g->encoderNumber.
static void buildYamlPath(char *out, size_t outLen, mcaster1Globals *g)
{
    if (strlen(g->gConfigFileName) == 0)
        _snprintf(out, outLen - 1, "Mcaster1 DSP Encoder_%d.yaml", g->encoderNumber);
    else
        _snprintf(out, outLen - 1, "%s_%d.yaml", g->gConfigFileName, g->encoderNumber);
    out[outLen - 1] = '\0';
}

// Build the legacy .cfg path (used for migration detection).
static void buildCfgPath(char *out, size_t outLen, mcaster1Globals *g)
{
    if (strlen(g->gConfigFileName) == 0)
        _snprintf(out, outLen - 1, "Mcaster1 DSP Encoder_%d.cfg", g->encoderNumber);
    else
        _snprintf(out, outLen - 1, "%s_%d.cfg", g->gConfigFileName, g->encoderNumber);
    out[outLen - 1] = '\0';
}

// Emit a YAML scalar pair  key: "value"
static int emitPair(yaml_emitter_t *e, const char *key, const char *value)
{
    yaml_event_t ev;

    // key — always plain
    if (!yaml_scalar_event_initialize(&ev, NULL, NULL,
            (yaml_char_t *)key, (int)strlen(key),
            1, 1, YAML_PLAIN_SCALAR_STYLE))
        return 0;
    if (!yaml_emitter_emit(e, &ev)) return 0;

    // value — use double-quoted when empty so YAML parsers don't confuse it
    yaml_scalar_style_t style =
        (value && value[0]) ? YAML_PLAIN_SCALAR_STYLE : YAML_DOUBLE_QUOTED_SCALAR_STYLE;

    const char *v = value ? value : "";
    if (!yaml_scalar_event_initialize(&ev, NULL, NULL,
            (yaml_char_t *)v, (int)strlen(v),
            1, 1, style))
        return 0;
    return yaml_emitter_emit(e, &ev);
}

static int emitPairInt(yaml_emitter_t *e, const char *key, long value)
{
    char buf[32];
    _snprintf(buf, sizeof(buf) - 1, "%ld", value);
    return emitPair(e, key, buf);
}

// Apply extended Windows-only fields that are NOT in the legacy config_read() mapping.
// Called from readConfigYAML after config_read() has already handled standard fields.
static void applyExtendedField(mcaster1Globals *g, const char *key, const char *val)
{
#ifdef WIN32
    if (strcmp(key, "OpusComplexity") == 0) { g->opusComplexity = atoi(val); return; }
    if (strcmp(key, "FdkAacProfile")  == 0) { g->fdkAacProfile  = atoi(val); return; }
    if (strcmp(key, "LameVBRMode2")   == 0) { g->lameVBRMode    = atoi(val); return; }
    if (strcmp(key, "LameVBRQuality") == 0) { g->lameVBRQuality = atoi(val); return; }
    if (strcmp(key, "LameABRMean")    == 0) { g->lameABRMean    = atoi(val); return; }
    if (strcmp(key, "LameMinBitrate") == 0) { g->lameMinBitrate = atoi(val); return; }
    if (strcmp(key, "LameMaxBitrate") == 0) { g->lameMaxBitrate = atoi(val); return; }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// writeConfigYAML
// ─────────────────────────────────────────────────────────────────────────────

int writeConfigYAML(mcaster1Globals *g)
{
    char path[1024];
    buildYamlPath(path, sizeof(path), g);

    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    yaml_emitter_t emitter;
    yaml_emitter_initialize(&emitter);
    yaml_emitter_set_output_file(&emitter, fp);
    yaml_emitter_set_unicode(&emitter, 1);

    yaml_event_t ev;
    int ok = 1;

#define EMIT(init_call) \
    do { if (!(init_call) || !yaml_emitter_emit(&emitter, &ev)) { ok = 0; goto done; } } while(0)
#define ESTR(k, v)   do { if (!emitPair(&emitter, (k), (v)))       { ok = 0; goto done; } } while(0)
#define EINT(k, v)   do { if (!emitPairInt(&emitter, (k), (long)(v))){ ok = 0; goto done; } } while(0)

    EMIT(yaml_stream_start_event_initialize(&ev, YAML_UTF8_ENCODING));
    EMIT(yaml_document_start_event_initialize(&ev, NULL, NULL, NULL, 1));
    EMIT(yaml_mapping_start_event_initialize(&ev, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE));

    // Metadata
    ESTR("version", YAML_CONFIG_VERSION);

    // ── Server ──────────────────────────────────────────────────────────────
    ESTR("ServerType",        g->gServerType);
    ESTR("Server",            g->gServer);
    ESTR("Port",              g->gPort);
    ESTR("ServerMountpoint",  g->gMountpoint);
    ESTR("ServerPassword",    g->gPassword);
    EINT("ServerPublic",      g->gPubServ);
    ESTR("ServerIRC",         g->gServIRC);
    ESTR("ServerAIM",         g->gServAIM);
    ESTR("ServerICQ",         g->gServICQ);
    ESTR("ServerStreamURL",   g->gServURL);
    ESTR("ServerDescription", g->gServDesc);
    ESTR("ServerName",        g->gServName);
    ESTR("ServerGenre",       g->gServGenre);
    ESTR("SourceURL",         g->gSourceURL);

    // ── Reconnect ────────────────────────────────────────────────────────────
    EINT("AutomaticReconnect",     g->gAutoReconnect);
    EINT("AutomaticReconnectSecs", g->gReconnectSec);
    EINT("AutoConnect",            g->autoconnect);

    // ── Encoder ──────────────────────────────────────────────────────────────
    ESTR("Encode",               g->gEncodeType);
    EINT("BitrateNominal",       g->currentBitrate);
    EINT("BitrateMin",           g->currentBitrateMin);
    EINT("BitrateMax",           g->currentBitrateMax);
    EINT("NumberChannels",       g->currentChannels);
    EINT("Samplerate",           (long)g->currentSamplerate);
    ESTR("OggQuality",           g->gOggQuality);
    ESTR("OggBitrateQualityFlag", g->gOggBitQualFlag ? "Bitrate" : "Quality");

    // ── LAME ─────────────────────────────────────────────────────────────────
    EINT("LameCBRFlag",           g->gLAMEOptions.cbrflag);
    EINT("LameQuality",           g->gLAMEOptions.quality);
    EINT("LameCopywrite",         g->gLAMEOptions.copywrite);
    EINT("LameOriginal",          g->gLAMEOptions.original);
    EINT("LameStrictISO",         g->gLAMEOptions.strict_ISO);
    EINT("LameDisableReservior",  g->gLAMEOptions.disable_reservoir);
    ESTR("LameVBRMode",           g->gLAMEOptions.VBR_mode);
    EINT("LameLowpassfreq",       g->gLAMEOptions.lowpassfreq);
    EINT("LameHighpassfreq",      g->gLAMEOptions.highpassfreq);
    EINT("LAMEPreset",            g->gLAMEpreset);
    EINT("LAMEJointStereo",       g->LAMEJointStereoFlag);

    // ── AAC ──────────────────────────────────────────────────────────────────
    ESTR("AACQuality", g->gAACQuality);
    ESTR("AACCutoff",  g->gAACCutoff);

    // ── Extended Windows codec fields (not in legacy INI) ────────────────────
#ifdef WIN32
    EINT("OpusComplexity", g->opusComplexity);
    EINT("FdkAacProfile",  g->fdkAacProfile);
    EINT("LameVBRMode2",   g->lameVBRMode);
    EINT("LameVBRQuality", g->lameVBRQuality);
    EINT("LameABRMean",    g->lameABRMean);
    EINT("LameMinBitrate", g->lameMinBitrate);
    EINT("LameMaxBitrate", g->lameMaxBitrate);
#endif

    // ── Recording ────────────────────────────────────────────────────────────
    ESTR("AdvRecDevice",    g->gAdvRecDevice);
    EINT("LiveInSamplerate", g->gLiveInSamplerate);
    EINT("LineInFlag",       g->gLiveRecordingFlag);
    ESTR("WindowsRecDevice", g->WindowsRecDevice);

    // ── Window position ──────────────────────────────────────────────────────
    EINT("lastX",       g->lastX);
    EINT("lastY",       g->lastY);
    EINT("lastDummyX",  g->lastDummyX);
    EINT("lastDummyY",  g->lastDummyY);
    EINT("showVU",      g->vuShow);

    // ── Metadata ─────────────────────────────────────────────────────────────
    ESTR("LockMetadata",       g->gManualSongTitle);
    EINT("LockMetadataFlag",   g->gLockSongTitle);
    ESTR("MetadataAppend",     g->metadataAppendString);
    ESTR("MetadataRemoveBefore", g->metadataRemoveStringBefore);
    ESTR("MetadataRemoveAfter",  g->metadataRemoveStringAfter);
    ESTR("MetadataWindowClass",  g->metadataWindowClass);
    EINT("MetadataWindowClassInd", (int)g->metadataWindowClassInd);

    // ── Advanced ─────────────────────────────────────────────────────────────
    ESTR("SaveDirectory",    g->gSaveDirectory);
    EINT("SaveDirectoryFlag", g->gSaveDirectoryFlag);
    EINT("LogLevel",         g->gLogLevel);
    EINT("SaveAsWAV",        g->gSaveAsWAV);
    ESTR("LogFile",          g->gLogFile);
    EINT("NumEncoders",      g->gNumEncoders);
    ESTR("OutputControl",    g->outputControl);

    // ── External metadata ────────────────────────────────────────────────────
    ESTR("ExternalMetadata", g->externalMetadata);
    ESTR("ExternalURL",      g->externalURL);
    ESTR("ExternalFile",     g->externalFile);
    ESTR("ExternalInterval", g->externalInterval);

    EMIT(yaml_mapping_end_event_initialize(&ev));
    EMIT(yaml_document_end_event_initialize(&ev, 1));
    EMIT(yaml_stream_end_event_initialize(&ev));
    yaml_emitter_flush(&emitter);

done:
#undef EMIT
#undef ESTR
#undef EINT
    yaml_emitter_delete(&emitter);
    fclose(fp);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// readConfigYAML
// ─────────────────────────────────────────────────────────────────────────────

int readConfigYAML(mcaster1Globals *g)
{
    char path[1024];
    buildYamlPath(path, sizeof(path), g);

    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;   // File not found — caller falls back to legacy INI

    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, fp);

    // Reset the shared config store before populating from YAML.
    // This mirrors what readConfigFile() does at the top of its body.
    configReset();

    // Simple state machine: we expect a flat mapping at the top level.
    // Any nesting is skipped gracefully (future extensibility).
    typedef enum { S_START, S_MAP, S_KEY } ParseState;
    ParseState state = S_START;
    char curKey[256] = "";
    yaml_event_t ev;
    int running = 1;

    while (running) {
        if (!yaml_parser_parse(&parser, &ev)) break;

        switch (state) {
        case S_START:
            if (ev.type == YAML_MAPPING_START_EVENT)
                state = S_MAP;
            break;

        case S_MAP:
            if (ev.type == YAML_SCALAR_EVENT) {
                strncpy(curKey, (char *)ev.data.scalar.value, sizeof(curKey) - 1);
                curKey[sizeof(curKey) - 1] = '\0';
                state = S_KEY;
            } else if (ev.type == YAML_MAPPING_END_EVENT ||
                       ev.type == YAML_STREAM_END_EVENT) {
                running = 0;
            }
            break;

        case S_KEY:
            if (ev.type == YAML_SCALAR_EVENT) {
                const char *val = (char *)ev.data.scalar.value;
                // Feed into shared config store (used by config_read below)
                if (strcmp(curKey, "version") != 0)
                    configAddKeyValue(curKey, val);
                // Also apply new extended fields directly to struct
                applyExtendedField(g, curKey, val);
                state = S_MAP;
            } else if (ev.type == YAML_MAPPING_START_EVENT ||
                       ev.type == YAML_SEQUENCE_START_EVENT) {
                // Skip unexpected nested structures gracefully
                int depth = 1;
                yaml_event_delete(&ev);
                while (depth > 0 && yaml_parser_parse(&parser, &ev)) {
                    if (ev.type == YAML_MAPPING_START_EVENT  ||
                        ev.type == YAML_SEQUENCE_START_EVENT) depth++;
                    if (ev.type == YAML_MAPPING_END_EVENT    ||
                        ev.type == YAML_SEQUENCE_END_EVENT)  depth--;
                    yaml_event_delete(&ev);
                }
                state = S_MAP;
                continue;  // ev already deleted, skip the delete below
            }
            break;
        }

        if (ev.type == YAML_STREAM_END_EVENT) running = 0;
        yaml_event_delete(&ev);
    }

    yaml_parser_delete(&parser);
    fclose(fp);

    // Apply the populated config store to the struct and fire callbacks
    // (bitrateCallback, streamTypeCallback, destURLCallback, etc.)
    config_read(g);

    return 1;
}
