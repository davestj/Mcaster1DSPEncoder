/*
 * Mcaster1 VoicTune — Voice Coaching Engine
 * voictune/vt_coach.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vt_coach.h"
#include <cmath>
#include <numeric>
#include <algorithm>

namespace mc1vt {

VoiceCoach::VoiceCoach() {
    reset();
}

void VoiceCoach::reset() {
    lufs_history_.clear();
    pitch_history_.clear();
    dynamic_range_history_.clear();
    silence_frames_  = 0;
    speech_frames_   = 0;
    total_frames_    = 0;
    speech_to_silence_transitions_ = 0;
    prev_was_speech_ = false;
    tip_counts_ = TipCounts{};

    auto now = std::chrono::steady_clock::now() - std::chrono::seconds(60);
    session_start_          = std::chrono::steady_clock::now();
    last_level_tip_         = now;
    last_pitch_tip_         = now;
    last_sibilance_tip_     = now;
    last_proximity_tip_     = now;
    last_pacing_tip_        = now;
    last_plosive_tip_       = now;
    last_room_noise_tip_    = now;
    last_dynamic_range_tip_ = now;
    last_wpm_tip_           = now;
}

bool VoiceCoach::can_tip(TimePoint& last) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last).count();
    return elapsed >= config.min_tip_interval_sec;
}

std::vector<CoachTip> VoiceCoach::analyze(const VoiceSnapshot& snap) {
    std::vector<CoachTip> tips;
    total_frames_++;

    /* Track speech→silence transitions for WPM estimation */
    if (snap.is_speech && !prev_was_speech_) {
        speech_to_silence_transitions_++;
    }
    prev_was_speech_ = snap.is_speech;

    if (snap.is_speech) {
        speech_frames_++;
        silence_frames_ = 0;

        /* Track history for trend analysis */
        lufs_history_.push_back(snap.lufs);
        if (lufs_history_.size() > 100) lufs_history_.erase(lufs_history_.begin());

        if (snap.pitch_hz > 50.0f) {
            pitch_history_.push_back(snap.pitch_hz);
            if (pitch_history_.size() > 100) pitch_history_.erase(pitch_history_.begin());
        }

        /* Track dynamic range history */
        dynamic_range_history_.push_back(snap.dynamic_range_db);
        if (dynamic_range_history_.size() > 100) dynamic_range_history_.erase(dynamic_range_history_.begin());
    } else {
        silence_frames_++;
    }

    check_level(snap, tips);
    check_peak(snap, tips);
    check_sibilance(snap, tips);
    check_proximity(snap, tips);
    check_pitch_drift(snap, tips);
    check_pacing(snap, tips);
    check_plosive(snap, tips);
    check_room_noise(snap, tips);
    check_dynamic_range(snap, tips);
    check_wpm(snap, tips);

    return tips;
}

void VoiceCoach::check_level(const VoiceSnapshot& s, std::vector<CoachTip>& tips) {
    if (!s.is_speech || !can_tip(last_level_tip_)) return;

    float diff = s.lufs - config.target_lufs;

    if (diff < -(config.lufs_tolerance + 6.0f)) {
        tips.push_back({CoachSeverity::WARNING, "level",
            "Very quiet — your level is " + std::to_string((int)std::abs(diff)) + " dB below target",
            "Move closer to the mic or increase your gain",
            0.9f});
        last_level_tip_ = std::chrono::steady_clock::now();
        tip_counts_.level++;
    } else if (diff < -config.lufs_tolerance) {
        tips.push_back({CoachSeverity::SUGGESTION, "level",
            "Slightly quiet — " + std::to_string((int)std::abs(diff)) + " dB below target LUFS",
            "Try speaking a bit louder or move slightly closer to the mic",
            0.7f});
        last_level_tip_ = std::chrono::steady_clock::now();
        tip_counts_.level++;
    } else if (diff > config.lufs_tolerance) {
        tips.push_back({CoachSeverity::WARNING, "level",
            "Too hot — " + std::to_string((int)diff) + " dB above target LUFS",
            "Back away from the mic slightly or reduce gain",
            0.8f});
        last_level_tip_ = std::chrono::steady_clock::now();
        tip_counts_.level++;
    }
}

void VoiceCoach::check_peak(const VoiceSnapshot& s, std::vector<CoachTip>& tips) {
    if (s.peak_db >= config.peak_ceiling_db) {
        tips.push_back({CoachSeverity::CRITICAL, "level",
            "Clipping detected! Peak at " + std::to_string((int)s.peak_db) + " dBFS",
            "Immediately reduce gain or move back from the mic",
            1.0f});
    }
}

void VoiceCoach::check_sibilance(const VoiceSnapshot& s, std::vector<CoachTip>& tips) {
    if (!s.is_speech || !can_tip(last_sibilance_tip_)) return;

    if (s.high_freq_energy > config.sibilance_thresh) {
        tips.push_back({CoachSeverity::SUGGESTION, "sibilance",
            "Harsh sibilance detected (S/T sounds are prominent)",
            "Try angling the mic slightly off-axis, or enable the de-esser effect",
            0.6f});
        last_sibilance_tip_ = std::chrono::steady_clock::now();
        tip_counts_.sibilance++;
    }
}

void VoiceCoach::check_proximity(const VoiceSnapshot& s, std::vector<CoachTip>& tips) {
    if (!s.is_speech || !can_tip(last_proximity_tip_)) return;

    if (s.low_freq_energy > config.proximity_thresh) {
        tips.push_back({CoachSeverity::SUGGESTION, "proximity",
            "Proximity effect — excessive low-end boom",
            "Move back 2-3 inches from the mic, or apply a high-pass filter at 80-100Hz",
            0.65f});
        last_proximity_tip_ = std::chrono::steady_clock::now();
        tip_counts_.proximity++;
    }
}

void VoiceCoach::check_pitch_drift(const VoiceSnapshot& s, std::vector<CoachTip>& tips) {
    if (!s.is_speech || pitch_history_.size() < 20 || !can_tip(last_pitch_tip_)) return;

    /* Check if pitch is drifting significantly from the speaker's average */
    float avg_pitch = std::accumulate(pitch_history_.begin(), pitch_history_.end(), 0.0f)
                      / pitch_history_.size();

    if (avg_pitch < 50.0f) return;

    /* Calculate drift in cents: 1200 * log2(current / average) */
    float drift_cents = 1200.0f * std::log2(s.pitch_hz / avg_pitch);

    if (std::abs(drift_cents) > config.pitch_drift_cents * 2.0f) {
        tips.push_back({CoachSeverity::SUGGESTION, "pitch",
            "Significant pitch change detected (" + std::to_string((int)drift_cents) + " cents from average)",
            "This may indicate vocal strain or fatigue — take a breath and relax your throat",
            0.5f});
        last_pitch_tip_ = std::chrono::steady_clock::now();
        tip_counts_.pitch++;
    }
}

void VoiceCoach::check_pacing(const VoiceSnapshot& s, std::vector<CoachTip>& tips) {
    if (total_frames_ < 50 || !can_tip(last_pacing_tip_)) return;

    float speech_pct = (float)speech_frames_ / total_frames_;

    /* Extended silence during a session */
    if (silence_frames_ > 150) { /* ~15 seconds at 10Hz analysis */
        tips.push_back({CoachSeverity::INFO, "pacing",
            "Extended pause detected",
            "Dead air — if intentional, great for dramatic effect. Otherwise, check your mic.",
            0.4f});
        last_pacing_tip_ = std::chrono::steady_clock::now();
        silence_frames_  = 0;
        tip_counts_.pacing++;
    }

    /* Speaking too much without pauses */
    if (speech_pct > 0.95f && total_frames_ > 300) {
        tips.push_back({CoachSeverity::INFO, "pacing",
            "You've been speaking continuously — consider adding natural pauses",
            "Brief pauses help listeners process information and add emphasis",
            0.3f});
        last_pacing_tip_ = std::chrono::steady_clock::now();
        tip_counts_.pacing++;
    }
}

/* ── VT-4: Enhanced coaching rules ──────────────────────────────────────────── */

void VoiceCoach::check_plosive(const VoiceSnapshot& s, std::vector<CoachTip>& tips) {
    if (!s.is_speech || !can_tip(last_plosive_tip_)) return;

    if (s.plosive_energy > config.plosive_thresh) {
        tips.push_back({CoachSeverity::SUGGESTION, "plosive",
            "Plosive detected (P/B pop)",
            "Use a pop filter or angle the mic 45 degrees off-axis",
            0.7f});
        last_plosive_tip_ = std::chrono::steady_clock::now();
        tip_counts_.plosive++;
    }
}

void VoiceCoach::check_room_noise(const VoiceSnapshot& s, std::vector<CoachTip>& tips) {
    if (s.is_speech || !can_tip(last_room_noise_tip_)) return;

    if (s.noise_floor_db > config.noise_floor_thresh) {
        int nf_int = (int)s.noise_floor_db;
        int gate_db = nf_int + 5;
        tips.push_back({CoachSeverity::WARNING, "room_noise",
            "Room noise detected at " + std::to_string(nf_int) + " dB",
            "Consider acoustic treatment or a noise gate at " + std::to_string(gate_db) + " dB",
            0.75f});
        last_room_noise_tip_ = std::chrono::steady_clock::now();
        tip_counts_.room_noise++;
    }
}

void VoiceCoach::check_dynamic_range(const VoiceSnapshot& s, std::vector<CoachTip>& tips) {
    if (!s.is_speech || dynamic_range_history_.size() < 20 || !can_tip(last_dynamic_range_tip_)) return;

    float avg_dr = std::accumulate(dynamic_range_history_.begin(),
                                    dynamic_range_history_.end(), 0.0f)
                   / dynamic_range_history_.size();

    if (avg_dr < config.dynamic_range_min) {
        tips.push_back({CoachSeverity::SUGGESTION, "dynamic_range",
            "Dynamic range too narrow (" + std::to_string((int)avg_dr) + " dB)",
            "Vary your vocal intensity — monotone delivery loses audience attention",
            0.6f});
        last_dynamic_range_tip_ = std::chrono::steady_clock::now();
        tip_counts_.dynamic_range++;
    }
}

void VoiceCoach::check_wpm(const VoiceSnapshot& s, std::vector<CoachTip>& tips) {
    if (total_frames_ < 100 || !can_tip(last_wpm_tip_)) return;

    int wpm = estimated_wpm();
    if (wpm <= 0) return;

    if (wpm < config.optimal_wpm_low) {
        tips.push_back({CoachSeverity::INFO, "wpm",
            "Speaking too slowly (~" + std::to_string(wpm) + " WPM)",
            "Pick up the pace slightly for broadcast energy",
            0.5f});
        last_wpm_tip_ = std::chrono::steady_clock::now();
        tip_counts_.wpm++;
    } else if (wpm > config.optimal_wpm_high) {
        tips.push_back({CoachSeverity::SUGGESTION, "wpm",
            "Speaking too fast (~" + std::to_string(wpm) + " WPM)",
            "Slow down and add natural pauses for clarity",
            0.55f});
        last_wpm_tip_ = std::chrono::steady_clock::now();
        tip_counts_.wpm++;
    }
}

/* ── VT-4: Session summary helpers ─────────────────────────────────────────── */

float VoiceCoach::avg_lufs() const {
    if (lufs_history_.empty()) return -96.0f;
    return std::accumulate(lufs_history_.begin(), lufs_history_.end(), 0.0f)
           / lufs_history_.size();
}

float VoiceCoach::avg_dynamic_range() const {
    if (dynamic_range_history_.empty()) return 0.0f;
    return std::accumulate(dynamic_range_history_.begin(),
                           dynamic_range_history_.end(), 0.0f)
           / dynamic_range_history_.size();
}

int VoiceCoach::estimated_wpm() const {
    /* Estimate WPM from silence→speech transitions over the analysis window.
     * Each transition roughly corresponds to a word or phrase boundary.
     * Analysis runs at ~10Hz, so total_frames / 10 = elapsed seconds.
     * Average English word takes ~0.4s to speak, and transitions approximate
     * phrase/word boundaries. We scale transitions to approximate WPM. */
    if (total_frames_ < 50) return 0;

    auto now = std::chrono::steady_clock::now();
    auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
        now - session_start_).count();
    if (elapsed_sec < 5) return 0;

    /* Each silence→speech transition approximates ~2-3 words on average.
     * Scale factor tuned for typical broadcast speech patterns. */
    float transitions_per_min = (float)speech_to_silence_transitions_ * 60.0f / (float)elapsed_sec;
    int wpm = (int)(transitions_per_min * 2.5f);
    return std::clamp(wpm, 0, 400);
}

} // namespace mc1vt
