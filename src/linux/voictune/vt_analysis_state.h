/*
 * Mcaster1 VoicTune — Thread-Safe Analysis State
 * voictune/vt_analysis_state.h
 *
 * Shared state between audio worker threads and HTTP API threads.
 * Meters and pitch use std::atomic<float> for lock-free HTTP reads.
 * Spectrum array and coach tips use std::mutex (brief locks).
 * Waveform ring buffer for oscilloscope display (~1000 samples).
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "vt_coach.h"
#include <atomic>
#include <vector>
#include <mutex>
#include <string>
#include <cstring>
#include <chrono>

namespace mc1vt {

class AnalysisState {
public:
    static constexpr int WAVEFORM_SIZE = 1024;

    AnalysisState() {
        waveform_.resize(WAVEFORM_SIZE, 0.0f);
    }

    /* ── Meters (atomic — lock-free reads from HTTP thread) ──────────── */

    void set_rms_db(float v)   { rms_db_.store(v, std::memory_order_relaxed); }
    void set_peak_db(float v)  { peak_db_.store(v, std::memory_order_relaxed); }
    void set_lufs(float v)     { lufs_.store(v, std::memory_order_relaxed); }
    void set_peak_hold_db(float v) { peak_hold_db_.store(v, std::memory_order_relaxed); }

    float rms_db()       const { return rms_db_.load(std::memory_order_relaxed); }
    float peak_db()      const { return peak_db_.load(std::memory_order_relaxed); }
    float lufs()         const { return lufs_.load(std::memory_order_relaxed); }
    float peak_hold_db() const { return peak_hold_db_.load(std::memory_order_relaxed); }

    /* ── Pitch (atomic — lock-free reads from HTTP thread) ───────────── */

    void set_pitch_hz(float v)     { pitch_hz_.store(v, std::memory_order_relaxed); }
    void set_pitch_confidence(float v) { pitch_confidence_.store(v, std::memory_order_relaxed); }
    void set_midi_note(int v)      { midi_note_.store(v, std::memory_order_relaxed); }
    void set_cents_off(float v)    { cents_off_.store(v, std::memory_order_relaxed); }

    float pitch_hz()         const { return pitch_hz_.load(std::memory_order_relaxed); }
    float pitch_confidence() const { return pitch_confidence_.load(std::memory_order_relaxed); }
    int   midi_note()        const { return midi_note_.load(std::memory_order_relaxed); }
    float cents_off()        const { return cents_off_.load(std::memory_order_relaxed); }

    /* Note name needs mutex (std::string is not atomic) */
    void set_note_name(const std::string& n) {
        std::lock_guard<std::mutex> lk(note_mtx_);
        note_name_ = n;
    }
    std::string note_name() const {
        std::lock_guard<std::mutex> lk(note_mtx_);
        return note_name_;
    }

    /* ── Spectral features (atomic) ──────────────────────────────────── */

    void set_spectral_centroid(float v) { spectral_centroid_.store(v, std::memory_order_relaxed); }
    void set_peak_frequency(float v)   { peak_frequency_.store(v, std::memory_order_relaxed); }

    float spectral_centroid() const { return spectral_centroid_.load(std::memory_order_relaxed); }
    float peak_frequency()    const { return peak_frequency_.load(std::memory_order_relaxed); }

    /* ── Spectrum array (mutex — brief lock for copy) ────────────────── */

    void set_spectrum(const std::vector<float>& bins) {
        std::lock_guard<std::mutex> lk(spectrum_mtx_);
        spectrum_ = bins;
    }
    void set_spectrum(std::vector<float>&& bins) {
        std::lock_guard<std::mutex> lk(spectrum_mtx_);
        spectrum_ = std::move(bins);
    }
    std::vector<float> spectrum() const {
        std::lock_guard<std::mutex> lk(spectrum_mtx_);
        return spectrum_;
    }

    /* ── Waveform ring buffer (mutex — brief lock) ───────────────────── */

    void push_waveform(const float* samples, int count) {
        std::lock_guard<std::mutex> lk(waveform_mtx_);
        for (int i = 0; i < count; ++i) {
            waveform_[waveform_pos_] = samples[i];
            waveform_pos_ = (waveform_pos_ + 1) % WAVEFORM_SIZE;
        }
    }

    std::vector<float> waveform() const {
        std::lock_guard<std::mutex> lk(waveform_mtx_);
        /* Return linearized from write position (oldest first) */
        std::vector<float> out(WAVEFORM_SIZE);
        for (int i = 0; i < WAVEFORM_SIZE; ++i) {
            out[i] = waveform_[(waveform_pos_ + i) % WAVEFORM_SIZE];
        }
        return out;
    }

    /* ── Coach tips (mutex — updated periodically) ───────────────────── */

    void set_tips(const std::vector<CoachTip>& tips) {
        std::lock_guard<std::mutex> lk(tips_mtx_);
        tips_ = tips;
        tips_updated_ = std::chrono::steady_clock::now();
    }
    std::vector<CoachTip> tips() const {
        std::lock_guard<std::mutex> lk(tips_mtx_);
        return tips_;
    }
    std::chrono::steady_clock::time_point tips_updated() const {
        std::lock_guard<std::mutex> lk(tips_mtx_);
        return tips_updated_;
    }

    /* ── Session state ───────────────────────────────────────────────── */

    void set_session_active(bool v) { session_active_.store(v, std::memory_order_relaxed); }
    bool session_active()     const { return session_active_.load(std::memory_order_relaxed); }

    void set_session_id(int v) { session_id_.store(v, std::memory_order_relaxed); }
    int  session_id()    const { return session_id_.load(std::memory_order_relaxed); }

    /* ── Analysis activity ───────────────────────────────────────────── */

    void set_analyzing(bool v)    { analyzing_.store(v, std::memory_order_relaxed); }
    bool analyzing()        const { return analyzing_.load(std::memory_order_relaxed); }

    void increment_chunk_count()  { chunk_count_.fetch_add(1, std::memory_order_relaxed); }
    int  chunk_count()      const { return chunk_count_.load(std::memory_order_relaxed); }

private:
    /* Meters */
    std::atomic<float> rms_db_{-96.0f};
    std::atomic<float> peak_db_{-96.0f};
    std::atomic<float> lufs_{-96.0f};
    std::atomic<float> peak_hold_db_{-96.0f};

    /* Pitch */
    std::atomic<float> pitch_hz_{0.0f};
    std::atomic<float> pitch_confidence_{0.0f};
    std::atomic<int>   midi_note_{0};
    std::atomic<float> cents_off_{0.0f};
    mutable std::mutex note_mtx_;
    std::string        note_name_;

    /* Spectral features */
    std::atomic<float> spectral_centroid_{0.0f};
    std::atomic<float> peak_frequency_{0.0f};

    /* Spectrum array */
    mutable std::mutex     spectrum_mtx_;
    std::vector<float>     spectrum_;

    /* Waveform ring buffer */
    mutable std::mutex     waveform_mtx_;
    std::vector<float>     waveform_;
    int                    waveform_pos_ = 0;

    /* Coach tips */
    mutable std::mutex     tips_mtx_;
    std::vector<CoachTip>  tips_;
    std::chrono::steady_clock::time_point tips_updated_;

    /* Session */
    std::atomic<bool>  session_active_{false};
    std::atomic<int>   session_id_{0};

    /* Activity */
    std::atomic<bool>  analyzing_{false};
    std::atomic<int>   chunk_count_{0};
};

} // namespace mc1vt
