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
/* Helper macro for strncpy into fixed-size struct field */
#define ASTR(field) do { strncpy(g->field, val, sizeof(g->field)-1); g->field[sizeof(g->field)-1]='\0'; } while(0)
#define AINT(field) do { g->field = atoi(val); } while(0)

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

    // ── Podcast RSS (Phase 5) ──────────────────────────────────────────────
    if (strcmp(key, "GenerateRSS")        == 0) { AINT(gGenerateRSS);       return; }
    if (strcmp(key, "RSSUseYPSettings")   == 0) { AINT(gRSSUseYPSettings);  return; }
    if (strcmp(key, "PodcastTitle")       == 0) { ASTR(gPodcastTitle);       return; }
    if (strcmp(key, "PodcastAuthor")      == 0) { ASTR(gPodcastAuthor);      return; }
    if (strcmp(key, "PodcastCategory")    == 0) { ASTR(gPodcastCategory);    return; }
    if (strcmp(key, "PodcastLanguage")    == 0) { ASTR(gPodcastLanguage);    return; }
    if (strcmp(key, "PodcastCopyright")   == 0) { ASTR(gPodcastCopyright);   return; }
    if (strcmp(key, "PodcastWebsite")     == 0) { ASTR(gPodcastWebsite);     return; }
    if (strcmp(key, "PodcastCoverArt")    == 0) { ASTR(gPodcastCoverArt);    return; }
    if (strcmp(key, "PodcastDescription") == 0) { ASTR(gPodcastDescription); return; }

    // ── ICY 2.2 Extended (Phase 5) ─────────────────────────────────────────
    if (strcmp(key, "ICY22StationID")          == 0) { ASTR(gICY22StationID);          return; }
    if (strcmp(key, "ICY22StationLogo")        == 0) { ASTR(gICY22StationLogo);        return; }
    if (strcmp(key, "ICY22VerifyStatus")       == 0) { ASTR(gICY22VerifyStatus);       return; }
    if (strcmp(key, "ICY22ShowTitle")          == 0) { ASTR(gICY22ShowTitle);          return; }
    if (strcmp(key, "ICY22ShowStart")          == 0) { ASTR(gICY22ShowStart);          return; }
    if (strcmp(key, "ICY22ShowEnd")            == 0) { ASTR(gICY22ShowEnd);            return; }
    if (strcmp(key, "ICY22NextShow")           == 0) { ASTR(gICY22NextShow);           return; }
    if (strcmp(key, "ICY22NextShowTime")       == 0) { ASTR(gICY22NextShowTime);       return; }
    if (strcmp(key, "ICY22ScheduleURL")        == 0) { ASTR(gICY22ScheduleURL);        return; }
    if (strcmp(key, "ICY22AutoDJ")             == 0) { AINT(gICY22AutoDJ);             return; }
    if (strcmp(key, "ICY22PlaylistName")       == 0) { ASTR(gICY22PlaylistName);       return; }
    if (strcmp(key, "ICY22DJHandle")           == 0) { ASTR(gICY22DJHandle);           return; }
    if (strcmp(key, "ICY22DJBio")              == 0) { ASTR(gICY22DJBio);              return; }
    if (strcmp(key, "ICY22DJGenre")            == 0) { ASTR(gICY22DJGenre);            return; }
    if (strcmp(key, "ICY22DJRating")           == 0) { ASTR(gICY22DJRating);           return; }
    if (strcmp(key, "ICY22CreatorHandle")      == 0) { ASTR(gICY22CreatorHandle);      return; }
    if (strcmp(key, "ICY22SocialTwitter")      == 0) { ASTR(gICY22SocialTwitter);      return; }
    if (strcmp(key, "ICY22SocialTwitch")       == 0) { ASTR(gICY22SocialTwitch);       return; }
    if (strcmp(key, "ICY22SocialIG")           == 0) { ASTR(gICY22SocialIG);           return; }
    if (strcmp(key, "ICY22SocialTikTok")       == 0) { ASTR(gICY22SocialTikTok);       return; }
    if (strcmp(key, "ICY22SocialYouTube")      == 0) { ASTR(gICY22SocialYouTube);      return; }
    if (strcmp(key, "ICY22SocialFacebook")     == 0) { ASTR(gICY22SocialFacebook);     return; }
    if (strcmp(key, "ICY22SocialLinkedIn")     == 0) { ASTR(gICY22SocialLinkedIn);     return; }
    if (strcmp(key, "ICY22SocialLinktree")     == 0) { ASTR(gICY22SocialLinktree);     return; }
    if (strcmp(key, "ICY22Emoji")              == 0) { ASTR(gICY22Emoji);              return; }
    if (strcmp(key, "ICY22Hashtags")           == 0) { ASTR(gICY22Hashtags);           return; }
    if (strcmp(key, "ICY22RequestEnabled")     == 0) { AINT(gICY22RequestEnabled);     return; }
    if (strcmp(key, "ICY22RequestURL")         == 0) { ASTR(gICY22RequestURL);         return; }
    if (strcmp(key, "ICY22ChatURL")            == 0) { ASTR(gICY22ChatURL);            return; }
    if (strcmp(key, "ICY22TipURL")             == 0) { ASTR(gICY22TipURL);             return; }
    if (strcmp(key, "ICY22EventsURL")          == 0) { ASTR(gICY22EventsURL);          return; }
    if (strcmp(key, "ICY22CrosspostPlatforms") == 0) { ASTR(gICY22CrosspostPlatforms); return; }
    if (strcmp(key, "ICY22SessionID")          == 0) { ASTR(gICY22SessionID);          return; }
    if (strcmp(key, "ICY22CDNRegion")          == 0) { ASTR(gICY22CDNRegion);          return; }
    if (strcmp(key, "ICY22RelayOrigin")        == 0) { ASTR(gICY22RelayOrigin);        return; }
    if (strcmp(key, "ICY22NSFW")               == 0) { AINT(gICY22NSFW);               return; }
    if (strcmp(key, "ICY22AIGenerator")        == 0) { AINT(gICY22AIGenerator);        return; }
    if (strcmp(key, "ICY22GeoRegion")          == 0) { ASTR(gICY22GeoRegion);          return; }
    if (strcmp(key, "ICY22LicenseType")        == 0) { ASTR(gICY22LicenseType);        return; }
    if (strcmp(key, "ICY22RoyaltyFree")        == 0) { AINT(gICY22RoyaltyFree);        return; }
    if (strcmp(key, "ICY22LicenseTerritory")   == 0) { ASTR(gICY22LicenseTerritory);   return; }
    if (strcmp(key, "ICY22NoticeText")         == 0) { ASTR(gICY22NoticeText);         return; }
    if (strcmp(key, "ICY22NoticeURL")          == 0) { ASTR(gICY22NoticeURL);          return; }
    if (strcmp(key, "ICY22NoticeExpires")      == 0) { ASTR(gICY22NoticeExpires);      return; }
    if (strcmp(key, "ICY22VideoType")          == 0) { ASTR(gICY22VideoType);          return; }
    if (strcmp(key, "ICY22VideoLink")          == 0) { ASTR(gICY22VideoLink);          return; }
    if (strcmp(key, "ICY22VideoTitle")         == 0) { ASTR(gICY22VideoTitle);         return; }
    if (strcmp(key, "ICY22VideoPoster")        == 0) { ASTR(gICY22VideoPoster);        return; }
    if (strcmp(key, "ICY22VideoPlatform")      == 0) { ASTR(gICY22VideoPlatform);      return; }
    if (strcmp(key, "ICY22VideoLive")          == 0) { AINT(gICY22VideoLive);          return; }
    if (strcmp(key, "ICY22VideoCodec")         == 0) { ASTR(gICY22VideoCodec);         return; }
    if (strcmp(key, "ICY22VideoFPS")           == 0) { ASTR(gICY22VideoFPS);           return; }
    if (strcmp(key, "ICY22VideoResolution")    == 0) { ASTR(gICY22VideoResolution);    return; }
    if (strcmp(key, "ICY22VideoRating")        == 0) { ASTR(gICY22VideoRating);        return; }
    if (strcmp(key, "ICY22VideoNSFW")          == 0) { AINT(gICY22VideoNSFW);          return; }
    if (strcmp(key, "ICY22LoudnessLUFS")       == 0) { ASTR(gICY22LoudnessLUFS);       return; }
#endif
}

#undef ASTR
#undef AINT

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

    // ── Podcast RSS (Phase 5) ─────────────────────────────────────────────────
    EINT("GenerateRSS",          g->gGenerateRSS);
    EINT("RSSUseYPSettings",     g->gRSSUseYPSettings);
    ESTR("PodcastTitle",         g->gPodcastTitle);
    ESTR("PodcastAuthor",        g->gPodcastAuthor);
    ESTR("PodcastCategory",      g->gPodcastCategory);
    ESTR("PodcastLanguage",      g->gPodcastLanguage);
    ESTR("PodcastCopyright",     g->gPodcastCopyright);
    ESTR("PodcastWebsite",       g->gPodcastWebsite);
    ESTR("PodcastCoverArt",      g->gPodcastCoverArt);
    ESTR("PodcastDescription",   g->gPodcastDescription);

    // ── ICY 2.2 Extended (Phase 5) ───────────────────────────────────────────
    ESTR("ICY22StationID",          g->gICY22StationID);
    ESTR("ICY22StationLogo",        g->gICY22StationLogo);
    ESTR("ICY22VerifyStatus",       g->gICY22VerifyStatus);
    ESTR("ICY22ShowTitle",          g->gICY22ShowTitle);
    ESTR("ICY22ShowStart",          g->gICY22ShowStart);
    ESTR("ICY22ShowEnd",            g->gICY22ShowEnd);
    ESTR("ICY22NextShow",           g->gICY22NextShow);
    ESTR("ICY22NextShowTime",       g->gICY22NextShowTime);
    ESTR("ICY22ScheduleURL",        g->gICY22ScheduleURL);
    EINT("ICY22AutoDJ",             g->gICY22AutoDJ);
    ESTR("ICY22PlaylistName",       g->gICY22PlaylistName);
    ESTR("ICY22DJHandle",           g->gICY22DJHandle);
    ESTR("ICY22DJBio",              g->gICY22DJBio);
    ESTR("ICY22DJGenre",            g->gICY22DJGenre);
    ESTR("ICY22DJRating",           g->gICY22DJRating);
    ESTR("ICY22CreatorHandle",      g->gICY22CreatorHandle);
    ESTR("ICY22SocialTwitter",      g->gICY22SocialTwitter);
    ESTR("ICY22SocialTwitch",       g->gICY22SocialTwitch);
    ESTR("ICY22SocialIG",           g->gICY22SocialIG);
    ESTR("ICY22SocialTikTok",       g->gICY22SocialTikTok);
    ESTR("ICY22SocialYouTube",      g->gICY22SocialYouTube);
    ESTR("ICY22SocialFacebook",     g->gICY22SocialFacebook);
    ESTR("ICY22SocialLinkedIn",     g->gICY22SocialLinkedIn);
    ESTR("ICY22SocialLinktree",     g->gICY22SocialLinktree);
    ESTR("ICY22Emoji",              g->gICY22Emoji);
    ESTR("ICY22Hashtags",           g->gICY22Hashtags);
    EINT("ICY22RequestEnabled",     g->gICY22RequestEnabled);
    ESTR("ICY22RequestURL",         g->gICY22RequestURL);
    ESTR("ICY22ChatURL",            g->gICY22ChatURL);
    ESTR("ICY22TipURL",             g->gICY22TipURL);
    ESTR("ICY22EventsURL",          g->gICY22EventsURL);
    ESTR("ICY22CrosspostPlatforms", g->gICY22CrosspostPlatforms);
    ESTR("ICY22SessionID",          g->gICY22SessionID);
    ESTR("ICY22CDNRegion",          g->gICY22CDNRegion);
    ESTR("ICY22RelayOrigin",        g->gICY22RelayOrigin);
    EINT("ICY22NSFW",               g->gICY22NSFW);
    EINT("ICY22AIGenerator",        g->gICY22AIGenerator);
    ESTR("ICY22GeoRegion",          g->gICY22GeoRegion);
    ESTR("ICY22LicenseType",        g->gICY22LicenseType);
    EINT("ICY22RoyaltyFree",        g->gICY22RoyaltyFree);
    ESTR("ICY22LicenseTerritory",   g->gICY22LicenseTerritory);
    ESTR("ICY22NoticeText",         g->gICY22NoticeText);
    ESTR("ICY22NoticeURL",          g->gICY22NoticeURL);
    ESTR("ICY22NoticeExpires",      g->gICY22NoticeExpires);
    ESTR("ICY22VideoType",          g->gICY22VideoType);
    ESTR("ICY22VideoLink",          g->gICY22VideoLink);
    ESTR("ICY22VideoTitle",         g->gICY22VideoTitle);
    ESTR("ICY22VideoPoster",        g->gICY22VideoPoster);
    ESTR("ICY22VideoPlatform",      g->gICY22VideoPlatform);
    EINT("ICY22VideoLive",          g->gICY22VideoLive);
    ESTR("ICY22VideoCodec",         g->gICY22VideoCodec);
    ESTR("ICY22VideoFPS",           g->gICY22VideoFPS);
    ESTR("ICY22VideoResolution",    g->gICY22VideoResolution);
    ESTR("ICY22VideoRating",        g->gICY22VideoRating);
    EINT("ICY22VideoNSFW",          g->gICY22VideoNSFW);
    ESTR("ICY22LoudnessLUFS",       g->gICY22LoudnessLUFS);

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
