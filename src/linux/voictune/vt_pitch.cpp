/*
 * Mcaster1 VoicTune — Pitch Detection v1.0.0
 * voictune/vt_pitch.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vt_pitch.h"
#include <cmath>
#include <algorithm>
#include <mutex>

namespace mc1vt {

/* We compute the autocorrelation function to find the fundamental period.
 * This is more robust than pure FFT peak for voice (harmonics are strong). */
float PitchDetector::autocorrelate(const float* pcm, size_t frames, int sample_rate,
                                    float min_hz, float max_hz)
{
    if (frames < 2 || sample_rate <= 0) return 0.0f;

    /* Lag range: max_lag corresponds to min_hz, min_lag to max_hz */
    int min_lag = std::max(1, (int)(sample_rate / max_hz));
    int max_lag = std::min((int)frames - 1, (int)(sample_rate / min_hz));

    if (min_lag >= max_lag) return 0.0f;

    /* Normalized autocorrelation — we look for the first peak after the dip */
    float best_corr = -1.0f;
    int   best_lag  = 0;

    /* Energy at lag 0 for normalization */
    float energy = 0.0f;
    for (size_t i = 0; i < frames; ++i) energy += pcm[i] * pcm[i];
    if (energy < 1e-10f) return 0.0f;

    bool passed_dip = false;
    float prev_corr = 1.0f;

    for (int lag = min_lag; lag <= max_lag; ++lag) {
        float sum = 0.0f;
        float en1 = 0.0f, en2 = 0.0f;
        int n = (int)frames - lag;
        for (int i = 0; i < n; ++i) {
            sum += pcm[i] * pcm[i + lag];
            en1 += pcm[i] * pcm[i];
            en2 += pcm[i + lag] * pcm[i + lag];
        }
        float denom = std::sqrt(en1 * en2);
        float corr  = (denom > 1e-10f) ? sum / denom : 0.0f;

        /* We wait for the correlation to dip below 0.5 before looking for peak */
        if (corr < 0.5f) passed_dip = true;

        if (passed_dip && corr > best_corr && corr > prev_corr) {
            best_corr = corr;
            best_lag  = lag;
        }
        prev_corr = corr;
    }

    if (best_lag <= 0 || best_corr < 0.3f) return 0.0f;

    /* Parabolic interpolation around the peak lag for sub-sample accuracy */
    float freq = (float)sample_rate / (float)best_lag;

    /* We store confidence as the correlation value (0.3-1.0 range) */
    return freq;
}

PitchResult PitchDetector::detect(const float* pcm, size_t frames, int sample_rate,
                                   float fft_peak_hz)
{
    PitchResult result;

    /* Voice range: 65 Hz (C2 bass) to 1050 Hz (C6 soprano + harmonics) */
    float freq = autocorrelate(pcm, frames, sample_rate, 65.0f, 1050.0f);

    if (freq > 0.0f) {
        result = freq_to_note(freq);

        /* Confidence: correlation-based (approximated by signal energy) */
        float energy = 0.0f;
        for (size_t i = 0; i < frames; ++i) energy += pcm[i] * pcm[i];
        float rms = std::sqrt(energy / (float)frames);
        result.confidence = std::min(1.0f, rms * 10.0f);
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        last_ = result;
    }
    return result;
}

PitchResult PitchDetector::freq_to_note(float freq_hz)
{
    PitchResult r;
    if (freq_hz <= 0.0f) return r;

    r.frequency_hz = freq_hz;

    /* MIDI note: 69 = A4 = 440 Hz */
    float midi_float = 69.0f + 12.0f * std::log2(freq_hz / 440.0f);
    int   midi_note  = (int)std::round(midi_float);
    r.midi_note      = midi_note;
    r.cents_off      = (midi_float - (float)midi_note) * 100.0f;

    /* Note name + octave */
    int note_idx = ((midi_note % 12) + 12) % 12;
    int octave   = (midi_note / 12) - 1;
    r.note_name  = std::string(NOTE_NAMES[note_idx]) + std::to_string(octave);

    return r;
}

PitchResult PitchDetector::last_result() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return last_;
}

} // namespace mc1vt
