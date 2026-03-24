/*
 * Mcaster1 VoicTune — Voice Coaching Engine
 * voictune/vt_coach.h
 *
 * Rule-based voice coaching — analyzes real-time meters, pitch, and
 * spectrum data to generate actionable tips for broadcasters/podcasters.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <string>
#include <vector>
#include <chrono>

namespace mc1vt {

enum class CoachSeverity {
    INFO,       /* Positive feedback / tip */
    SUGGESTION, /* Gentle nudge */
    WARNING,    /* Needs attention */
    CRITICAL    /* Immediate action needed */
};

struct CoachTip {
    CoachSeverity severity;
    std::string   category;    /* "level", "pitch", "sibilance", "proximity", "pacing" */
    std::string   message;
    std::string   suggestion;  /* Actionable fix */
    float         confidence;  /* 0.0-1.0 how confident the detection is */
};

/* Snapshot of current voice state for the coach to analyze */
struct VoiceSnapshot {
    float rms_db     = -96.0f;
    float peak_db    = -96.0f;
    float lufs       = -96.0f;
    float pitch_hz   = 0.0f;
    float cents_off  = 0.0f;
    std::string note_name;

    /* Spectral features */
    float spectral_centroid_hz = 0.0f;
    float high_freq_energy     = 0.0f;   /* 4-8kHz energy ratio */
    float low_freq_energy      = 0.0f;   /* 80-300Hz energy ratio */

    /* Time-domain */
    bool  is_speech     = false;   /* true if signal is above noise floor */
    float speech_ratio  = 0.0f;   /* ratio of speech time in analysis window */

    /* VT-4: Enhanced coaching fields */
    float dynamic_range_db = 0.0f;    /* peak - rms */
    int   silence_gap_count = 0;       /* number of silence gaps in analysis window */
    float plosive_energy = 0.0f;       /* sub-80Hz transient energy */
    float noise_floor_db = -96.0f;     /* broadband noise during silence */
};

class VoiceCoach {
public:
    VoiceCoach();

    /* Analyze a snapshot and return coaching tips.
     * Maintains internal state for time-based rules (pitch drift, pacing). */
    std::vector<CoachTip> analyze(const VoiceSnapshot& snap);

    /* Reset internal state (call at session start) */
    void reset();

    /* Configuration thresholds */
    struct Config {
        float target_lufs      = -16.0f;  /* ITU broadcast target */
        float lufs_tolerance   = 3.0f;    /* +/- dB before warning */
        float peak_ceiling_db  = -1.0f;   /* Peak clipping threshold */
        float noise_floor_db   = -50.0f;  /* Below this = silence */
        float sibilance_thresh = 0.35f;   /* High-freq energy ratio */
        float proximity_thresh = 0.40f;   /* Low-freq energy ratio */
        float pitch_drift_cents = 50.0f;  /* Cents drift before warning */
        int   min_tip_interval_sec = 10;  /* Don't spam tips */

        /* VT-4: Enhanced coaching thresholds */
        float plosive_thresh = 0.5f;       /* sub-80Hz energy spike ratio */
        float noise_floor_thresh = -50.0f; /* dB above which = noisy room */
        float dynamic_range_min = 6.0f;    /* less than this = too narrow */
        int   optimal_wpm_low = 140;
        int   optimal_wpm_high = 170;
    } config;

    /* VT-4: Session-level tip counters for summary scoring */
    struct TipCounts {
        int level     = 0;
        int pitch     = 0;
        int sibilance = 0;
        int plosive   = 0;
        int room_noise = 0;
        int dynamic_range = 0;
        int wpm       = 0;
        int proximity = 0;
        int pacing    = 0;
    };
    TipCounts tip_counts() const { return tip_counts_; }
    int   total_frames() const { return total_frames_; }
    int   speech_frames() const { return speech_frames_; }
    float avg_lufs() const;
    float avg_dynamic_range() const;
    int   estimated_wpm() const;

private:
    using TimePoint = std::chrono::steady_clock::time_point;

    /* Internal tracking state */
    std::vector<float> lufs_history_;
    std::vector<float> pitch_history_;
    int  silence_frames_    = 0;
    int  speech_frames_     = 0;
    int  total_frames_      = 0;

    /* VT-4: Enhanced tracking */
    std::vector<float> dynamic_range_history_;
    int  speech_to_silence_transitions_ = 0;
    bool prev_was_speech_   = false;
    TipCounts tip_counts_;
    TimePoint session_start_;

    TimePoint last_level_tip_;
    TimePoint last_pitch_tip_;
    TimePoint last_sibilance_tip_;
    TimePoint last_proximity_tip_;
    TimePoint last_pacing_tip_;
    TimePoint last_plosive_tip_;
    TimePoint last_room_noise_tip_;
    TimePoint last_dynamic_range_tip_;
    TimePoint last_wpm_tip_;

    bool can_tip(TimePoint& last) const;

    void check_level(const VoiceSnapshot& s, std::vector<CoachTip>& tips);
    void check_peak(const VoiceSnapshot& s, std::vector<CoachTip>& tips);
    void check_sibilance(const VoiceSnapshot& s, std::vector<CoachTip>& tips);
    void check_proximity(const VoiceSnapshot& s, std::vector<CoachTip>& tips);
    void check_pitch_drift(const VoiceSnapshot& s, std::vector<CoachTip>& tips);
    void check_pacing(const VoiceSnapshot& s, std::vector<CoachTip>& tips);
    void check_plosive(const VoiceSnapshot& s, std::vector<CoachTip>& tips);
    void check_room_noise(const VoiceSnapshot& s, std::vector<CoachTip>& tips);
    void check_dynamic_range(const VoiceSnapshot& s, std::vector<CoachTip>& tips);
    void check_wpm(const VoiceSnapshot& s, std::vector<CoachTip>& tips);
};

} // namespace mc1vt
