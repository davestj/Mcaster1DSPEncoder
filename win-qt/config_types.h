/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * config_types.h — Shared configuration structs
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_CONFIG_TYPES_H
#define MC1_CONFIG_TYPES_H

#include <string>
#include <vector>
#include <cstdint>

/* DSP config structs for per-encoder parameter persistence */
#include "dsp/dsp_chain.h"

namespace mc1 {

struct SocketConfig {
    uint16_t    port           = 8330;
    std::string bind_address   = "0.0.0.0";
    bool        ssl_enabled    = false;
    std::string ssl_cert;
    std::string ssl_key;
};

struct DnasConfig {
    std::string host      = "localhost";
    uint16_t    port      = 8000;
    std::string username  = "source";
    std::string password;
    std::string stats_url = "/stats";
    bool        ssl       = false;
};

struct AdminConfig {
    std::string               username       = "admin";
    std::string               password       = "changeme";
    std::string               api_token;
    std::string               log_dir        = "/tmp";
    int                       log_level      = 4;
    int                       session_timeout = 3600;
    std::vector<SocketConfig> sockets;
    DnasConfig                dnas;
    std::string               webroot;
};

struct StreamTarget {
    enum class Protocol { ICECAST2, SHOUTCAST_V1, SHOUTCAST_V2, LIVE365, MCASTER1_DNAS, YOUTUBE, TWITCH };

    Protocol    protocol       = Protocol::ICECAST2;
    std::string host           = "localhost";
    uint16_t    port           = 8000;
    std::string mount          = "/stream";
    std::string username       = "source";
    std::string password;

    std::string station_name;
    std::string description;
    std::string genre;
    std::string url;
    std::string content_type   = "audio/mpeg";
    int         bitrate        = 128;
    int         sample_rate    = 44100;
    int         channels       = 2;

    /* ICY 2.2 extended identity */
    std::string icy2_twitter;
    std::string icy2_facebook;
    std::string icy2_instagram;
    std::string icy2_email;
    std::string icy2_language;
    std::string icy2_country;
    std::string icy2_city;
    bool        icy2_is_public = true;

    /* Reconnect */
    int  retry_interval_sec    = 5;
    int  max_retries           = -1; /* -1 = forever */
};

/* ICY 2.2 extended metadata — all fields from the spec */
struct ICY22Config {
    /* Session */
    std::string session_id;

    /* Station & Show */
    std::string station_id;
    std::string station_logo;
    std::string verify_status = "unverified"; /* unverified/pending/verified/gold */
    std::string show_title;
    std::string show_start;
    std::string show_end;
    std::string next_show;
    std::string next_show_time;
    std::string schedule_url;
    bool        auto_dj        = false;
    std::string playlist_name;

    /* DJ & Host */
    std::string dj_handle;
    std::string dj_bio;
    std::string dj_genre;
    std::string dj_rating;

    /* Social & Discovery */
    std::string creator_handle;
    std::string social_twitter;
    std::string social_twitch;
    std::string social_instagram;
    std::string social_tiktok;
    std::string social_youtube;
    std::string social_facebook;
    std::string social_linkedin;
    std::string social_linktree;
    std::string emoji;
    std::string hashtags;

    /* Engagement */
    bool        request_enabled = false;
    std::string request_url;
    std::string chat_url;
    std::string tip_url;
    std::string events_url;
    std::string crosspost_platforms;
    std::string cdn_region;
    std::string relay_origin;

    /* Compliance & Legal */
    bool        nsfw           = false;
    bool        ai_generated   = false;
    bool        royalty_free   = false;
    std::string license_type   = "all-rights-reserved";
    std::string license_territory = "GLOBAL";
    std::string geo_region;
    std::string notice_text;
    std::string notice_url;
    std::string notice_expires;
    std::string loudness_lufs;

    /* Video / Simulcast */
    std::string video_type     = "live";     /* live/short/clip/trailer/ad */
    std::string video_link;
    std::string video_title;
    std::string video_poster;
    std::string video_platform;              /* youtube/tiktok/twitch/kick/rumble/vimeo/custom */
    bool        video_live     = false;
    std::string video_codec;
    std::string video_fps;
    std::string video_resolution;
    std::string video_rating;
    bool        video_nsfw     = false;
};

/* YP (Yellow Pages) directory metadata */
struct YPConfig {
    bool        is_public      = false;
    std::string stream_name;
    std::string stream_desc;
    std::string stream_url;
    std::string stream_genre;
    std::string icq;
    std::string aim;
    std::string irc;
};

/* Podcast / Archive settings */
struct PodcastConfig {
    bool        generate_rss   = false;
    bool        use_yp_settings = false;
    std::string title;
    std::string author;
    std::string category;
    std::string language       = "en-us";
    std::string copyright;
    std::string website;
    std::string cover_art_url;
    std::string description;

    /* Phase M7: Output directories */
    std::string rss_output_dir;        // local dir for RSS XML feed
    std::string episode_output_dir;    // local dir for recorded episodes
    std::string episode_filename_tpl = "{title}-{date}"; // filename template

    /* Phase M7: SFTP upload */
    bool        sftp_enabled   = false;
    std::string sftp_host;
    uint16_t    sftp_port      = 22;
    std::string sftp_username;
    std::string sftp_password;         // empty if using key auth
    std::string sftp_key_path;         // SSH private key path
    std::string sftp_remote_dir;       // remote dir on SFTP server

    /* Phase M7: Simulcast to DNAS /podcast mount */
    bool        simulcast_dnas = false;

    /* Phase M7: Publishing destinations */
    enum class PublishTarget { SFTP, WORDPRESS, BUZZSPROUT, TRANSISTOR, PODBEAN };

    struct WordPressConfig {
        bool        enabled = false;
        std::string site_url;              // https://example.com
        std::string username;
        std::string app_password;          // WP Application Password
        std::string post_type = "post";    // "post" or "podcast" (for SSP)
    };

    struct BuzzsproutConfig {
        bool        enabled = false;
        std::string api_token;
        std::string podcast_id;
    };

    struct TransistorConfig {
        bool        enabled = false;
        std::string api_key;
        std::string show_id;
    };

    struct PodbeanConfig {
        bool        enabled = false;
        std::string client_id;
        std::string client_secret;
    };

    WordPressConfig   wordpress;
    BuzzsproutConfig  buzzsprout;
    TransistorConfig  transistor;
    PodbeanConfig     podbean;
};

/* Metadata source configuration */
struct MetadataConfig {
    enum class Source { MANUAL, URL, FILE, DISABLED };
    Source       source         = Source::DISABLED;
    bool        lock_metadata  = false;
    std::string manual_text;
    std::string append_string;
    std::string meta_url;
    std::string meta_file;
    int         update_interval_sec = 10;
};

/* Phase M6/M7: Video capture + encoding settings */
struct VideoConfig {
    bool        enabled      = false;
    int         device_index = 0;
    int         width        = 1280;
    int         height       = 720;
    int         fps          = 30;
    int         bitrate_kbps = 2500;
    std::string codec        = "h264";

    /* Phase M7: Video codec + container enums */
    enum class VideoCodec     { H264, THEORA, VP8, VP9 };
    enum class VideoContainer { FLV, OGG, WEBM, MKV };
    VideoCodec     video_codec     = VideoCodec::H264;
    VideoContainer video_container = VideoContainer::FLV;
};

struct EncoderConfig {
    int         slot_id        = 0;
    std::string name;

    /* Phase M7: Encoder category */
    enum class EncoderType { RADIO, PODCAST, TV_VIDEO };
    EncoderType encoder_type   = EncoderType::RADIO;

    /* Per-slot config file path (Phase M7) */
    std::string config_file_path;

    enum class InputType { DEVICE, PLAYLIST, URL };
    InputType   input_type     = InputType::PLAYLIST;
    int         device_index   = -1;
    std::string playlist_path;

    enum class Codec { MP3, VORBIS, OPUS, FLAC, AAC_LC, AAC_HE, AAC_HE_V2, AAC_ELD };
    Codec       codec          = Codec::MP3;
    int         bitrate_kbps   = 128;
    int         quality        = 5;
    int         sample_rate    = 44100;
    int         channels       = 2;

    enum class EncodeMode  { CBR, VBR, ABR };
    enum class ChannelMode { JOINT, STEREO, MONO };
    EncodeMode  encode_mode    = EncodeMode::CBR;
    ChannelMode channel_mode   = ChannelMode::JOINT;

    StreamTarget stream_target;

    /* DSP */
    bool        dsp_eq_enabled       = false;
    bool        dsp_agc_enabled      = false;
    bool        dsp_crossfade_enabled = true;
    float       dsp_crossfade_duration = 3.0f;
    std::string dsp_eq_preset        = "flat";

    /* Phase M6: EQ mode (0 = parametric 10-band, 1 = graphic 31-band) */
    int         dsp_eq_mode          = 0;
    std::string dsp_eq31_preset      = "flat";

    /* Phase M6: Sonic Enhancer (BBE Sonic Maximizer clone) */
    bool        dsp_sonic_enabled    = false;
    std::string dsp_sonic_preset     = "flat";

    /* Phase M8: Push-to-Talk ducking */
    bool        dsp_ptt_duck_enabled = false;

    /* Phase M8: DBX 286s voice processor */
    bool        dsp_dbx_voice_enabled = false;
    std::string dsp_dbx_preset        = "flat";

    /* AGC preset name (for dialog dropdown restore) */
    std::string dsp_agc_preset        = "default";

    /* Phase M10: Full DSP parameter persistence (per-encoder) */
    mc1dsp::AgcConfig            agc_params;
    mc1dsp::SonicEnhancerConfig  sonic_params;
    mc1dsp::PttDuckConfig        ptt_duck_params;
    mc1dsp::DbxVoiceConfig       dbx_voice_params;
    mc1dsp::CrossfaderConfig     crossfader_params;

    struct Eq10Params {
        mc1dsp::EqBandConfig bands[10];
        bool linked = true;
    };
    Eq10Params eq10_params;

    struct Eq31Params {
        float gains_l[31] = {};
        float gains_r[31] = {};
        bool  linked = true;
    };
    Eq31Params eq31_params;

    /* Playlist */
    bool        shuffle        = false;
    bool        repeat_all     = true;

    /* Archive */
    bool        archive_enabled = false;
    bool        archive_wav     = false;
    std::string archive_dir;

    /* Auto-start */
    bool        auto_start     = false;
    int         auto_start_delay_sec = 0;

    /* Reconnect */
    bool        auto_reconnect = true;
    int         reconnect_interval_sec = 5;
    int         reconnect_max_attempts = 0; /* 0 = unlimited */

    /* Logging */
    int         log_level      = 4;
    std::string log_file;

    /* Extended metadata */
    YPConfig       yp;
    PodcastConfig  podcast;
    ICY22Config    icy22;
    MetadataConfig metadata;

    /* Per-encoder audio device override (-1 = use global) */
    int            per_encoder_device_index = -1;

    /* Per-encoder metadata (vs global) */
    bool           use_global_metadata = true;
    MetadataConfig per_encoder_metadata;

    /* Phase M6: Video */
    VideoConfig    video;
};

/* ── Global application settings ────────────────────────────────────── */
struct GlobalConfig {
    /* Audio device */
    int         audio_device_index = -1;    /* PortAudio device index (-1 = system default) */
    std::string audio_device_uid;           /* CoreAudio UID (persistent across reboots) */

    /* Video device (for TV_VIDEO encoders) */
    int         video_device_index = 0;

    /* Window geometry */
    int         window_x           = -1;    /* -1 = system default */
    int         window_y           = -1;
    int         window_width       = 900;
    int         window_height      = 620;
    bool        window_maximized   = false;

    /* Theme (0 = dark, 1 = light) */
    int         theme              = 0;

    /* Logging */
    int         log_level          = 4;     /* 1=CRITICAL..5=DEBUG */
    std::string log_dir;

    /* Polling / timers */
    int         dnas_poll_sec      = 15;

    /* Startup & close behaviour */
    bool        auto_start         = false; /* auto-start all encoders on launch */
    bool        tray_on_close      = true;  /* minimize to tray instead of quit */

    /* User directories */
    std::string playlist_dir;
    std::string archive_dir;
};

/* ── DSP effects rack global defaults ──────────────────────────────── */
struct DspRackDefaults {
    mc1dsp::AgcConfig            agc;
    mc1dsp::SonicEnhancerConfig  sonic;
    mc1dsp::PttDuckConfig        ptt_duck;
    mc1dsp::DbxVoiceConfig       dbx_voice;
    mc1dsp::CrossfaderConfig     crossfader;
    EncoderConfig::Eq10Params    eq10;
    EncoderConfig::Eq31Params    eq31;

    /* Preset names for dialog dropdown restore */
    std::string agc_preset    = "default";
    std::string sonic_preset  = "flat";
    std::string dbx_preset    = "flat";
    std::string eq10_preset   = "flat";
    std::string eq31_preset   = "flat";
};

/* Encoder state (mirrors Linux encoder_slot.h) */
enum class SlotState {
    IDLE, STARTING, CONNECTING, LIVE,
    RECONNECTING, SLEEP, ERROR, STOPPING
};

inline const char* slot_state_str(SlotState s) {
    switch (s) {
        case SlotState::IDLE:         return "IDLE";
        case SlotState::STARTING:     return "STARTING";
        case SlotState::CONNECTING:   return "CONNECTING";
        case SlotState::LIVE:         return "LIVE";
        case SlotState::RECONNECTING: return "RECONNECTING";
        case SlotState::SLEEP:        return "SLEEP";
        case SlotState::ERROR:        return "ERROR";
        case SlotState::STOPPING:     return "STOPPING";
    }
    return "UNKNOWN";
}

inline const char* encoder_type_str(EncoderConfig::EncoderType t) {
    switch (t) {
        case EncoderConfig::EncoderType::RADIO:    return "Radio";
        case EncoderConfig::EncoderType::PODCAST:  return "Podcast";
        case EncoderConfig::EncoderType::TV_VIDEO: return "TV/Video";
    }
    return "Unknown";
}

inline const char* video_codec_str(VideoConfig::VideoCodec c) {
    switch (c) {
        case VideoConfig::VideoCodec::H264:   return "H.264";
        case VideoConfig::VideoCodec::THEORA: return "Theora";
        case VideoConfig::VideoCodec::VP8:    return "VP8";
        case VideoConfig::VideoCodec::VP9:    return "VP9";
    }
    return "Unknown";
}

inline const char* video_container_str(VideoConfig::VideoContainer c) {
    switch (c) {
        case VideoConfig::VideoContainer::FLV:  return "FLV";
        case VideoConfig::VideoContainer::OGG:  return "OGG";
        case VideoConfig::VideoContainer::WEBM: return "WebM";
        case VideoConfig::VideoContainer::MKV:  return "MKV";
    }
    return "Unknown";
}

inline const char* codec_str(EncoderConfig::Codec c) {
    switch (c) {
        case EncoderConfig::Codec::MP3:       return "MP3";
        case EncoderConfig::Codec::VORBIS:    return "Vorbis";
        case EncoderConfig::Codec::OPUS:      return "Opus";
        case EncoderConfig::Codec::FLAC:      return "FLAC";
        case EncoderConfig::Codec::AAC_LC:    return "AAC-LC";
        case EncoderConfig::Codec::AAC_HE:    return "HE-AAC";
        case EncoderConfig::Codec::AAC_HE_V2: return "HE-AAC v2";
        case EncoderConfig::Codec::AAC_ELD:   return "AAC-ELD";
    }
    return "Unknown";
}

} // namespace mc1

#endif // MC1_CONFIG_TYPES_H
