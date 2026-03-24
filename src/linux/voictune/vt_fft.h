/*
 * Mcaster1 VoicTune — FFT Analysis Engine v1.0.0
 * voictune/vt_fft.h
 *
 * Wraps kiss_fft for real-time spectrum analysis. Produces magnitude spectrum
 * in dB, peak frequency, and spectral centroid from PCM audio buffers.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <vector>
#include <cstddef>
#include <mutex>

namespace mc1vt {

class FftAnalyzer {
public:
    explicit FftAnalyzer(int fft_size = 4096, int sample_rate = 48000);
    ~FftAnalyzer();

    /* We process a mono float32 PCM buffer and update the magnitude spectrum.
     * Thread-safe — can be called from worker pool threads. */
    void analyze(const float* pcm, size_t frames);

    /* We return the last computed magnitude spectrum in dB (fft_size/2 + 1 bins).
     * Thread-safe copy — safe to call from HTTP thread. */
    std::vector<float> get_magnitude_db() const;

    /* We return the peak frequency in Hz from the last analysis. */
    float peak_frequency_hz() const;

    /* We return the spectral centroid in Hz (brightness indicator). */
    float spectral_centroid_hz() const;

    /* We return the bin count (fft_size / 2 + 1). */
    int bin_count() const { return fft_size_ / 2 + 1; }

    /* We return the frequency resolution per bin (sample_rate / fft_size). */
    float bin_resolution_hz() const { return (float)sample_rate_ / (float)fft_size_; }

    void set_sample_rate(int sr) { sample_rate_ = sr; }
    int  sample_rate() const { return sample_rate_; }
    int  fft_size() const { return fft_size_; }

private:
    int   fft_size_;
    int   sample_rate_;
    void* kiss_cfg_;   /* kiss_fft_cfg — opaque to avoid header leak */

    /* Windowed input buffer */
    std::vector<float> window_;
    std::vector<float> windowed_buf_;

    /* Output spectrum (magnitude in dB, fft_size/2 + 1 bins) */
    mutable std::mutex spectrum_mtx_;
    std::vector<float> magnitude_db_;
    float              peak_freq_hz_       = 0.0f;
    float              spectral_centroid_  = 0.0f;

    void build_hann_window();
};

} // namespace mc1vt
