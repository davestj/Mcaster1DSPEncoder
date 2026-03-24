/*
 * Mcaster1 VoicTune — Pitch Detection v1.0.0
 * voictune/vt_pitch.h
 *
 * Musical pitch detection using FFT peak + autocorrelation refinement.
 * Maps detected frequency to note name, octave, and cents deviation.
 * A4 = 440 Hz equal temperament standard.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <string>
#include <atomic>
#include <mutex>

namespace mc1vt {

struct PitchResult {
    float       frequency_hz = 0.0f;
    std::string note_name;            /* "A4", "C#3", "Bb5", etc. */
    int         midi_note    = 0;     /* MIDI note number (A4 = 69) */
    float       cents_off    = 0.0f;  /* -50 to +50 cents deviation */
    float       confidence   = 0.0f;  /* 0.0-1.0 detection confidence */
};

class PitchDetector {
public:
    PitchDetector() = default;

    /* We detect pitch from a mono PCM buffer using autocorrelation.
     * fft_peak_hz is optional hint from FFT analysis for faster convergence. */
    PitchResult detect(const float* pcm, size_t frames, int sample_rate,
                       float fft_peak_hz = 0.0f);

    /* We convert a frequency to its nearest note name + cents. */
    static PitchResult freq_to_note(float freq_hz);

    /* Thread-safe last result */
    PitchResult last_result() const;

private:
    mutable std::mutex mtx_;
    PitchResult last_;

    float autocorrelate(const float* pcm, size_t frames, int sample_rate,
                        float min_hz, float max_hz);
};

/* Note name lookup table */
static const char* NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

} // namespace mc1vt
