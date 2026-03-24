/*
 * Mcaster1 VoicTune — FFT Analysis Engine v1.0.0
 * voictune/vt_fft.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* We compile kiss_fft inline — it's a single C file, BSD-3 license.
 * Including the .c directly avoids needing a separate C compilation unit. */
extern "C" {
#include "../external/include/kiss_fft.h"
#include "../external/include/kiss_fft.c"
}

#include "vt_fft.h"
#include "vt_logger.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace mc1vt {

FftAnalyzer::FftAnalyzer(int fft_size, int sample_rate)
    : fft_size_(fft_size)
    , sample_rate_(sample_rate)
    , kiss_cfg_(nullptr)
{
    kiss_cfg_ = kiss_fft_alloc(fft_size, 0 /* forward */, nullptr, nullptr);
    magnitude_db_.resize(fft_size / 2 + 1, -96.0f);
    windowed_buf_.resize(fft_size, 0.0f);
    build_hann_window();
    VT_INFO("FFT analyzer initialized: size=" + std::to_string(fft_size) +
            " bins=" + std::to_string(bin_count()) +
            " resolution=" + std::to_string(bin_resolution_hz()) + "Hz");
}

FftAnalyzer::~FftAnalyzer()
{
    if (kiss_cfg_) kiss_fft_free(kiss_cfg_);
}

void FftAnalyzer::build_hann_window()
{
    window_.resize(fft_size_);
    for (int i = 0; i < fft_size_; ++i) {
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (float)(fft_size_ - 1)));
    }
}

void FftAnalyzer::analyze(const float* pcm, size_t frames)
{
    if (!kiss_cfg_ || !pcm) return;

    /* We take up to fft_size_ samples, zero-pad if shorter */
    int n = std::min((int)frames, fft_size_);
    for (int i = 0; i < n; ++i) {
        windowed_buf_[i] = pcm[i] * window_[i];
    }
    for (int i = n; i < fft_size_; ++i) {
        windowed_buf_[i] = 0.0f;
    }

    /* Run FFT — kiss_fft expects kiss_fft_cpx arrays */
    std::vector<kiss_fft_cpx> fin(fft_size_);
    std::vector<kiss_fft_cpx> fout(fft_size_);
    for (int i = 0; i < fft_size_; ++i) {
        fin[i].r = windowed_buf_[i];
        fin[i].i = 0.0f;
    }
    kiss_fft((kiss_fft_cfg)kiss_cfg_, fin.data(), fout.data());

    /* Compute magnitude spectrum in dB */
    int bins = fft_size_ / 2 + 1;
    std::vector<float> mag_db(bins);
    float peak_mag   = 0.0f;
    int   peak_bin   = 0;
    float centroid_num = 0.0f;
    float centroid_den = 0.0f;

    for (int i = 0; i < bins; ++i) {
        float re  = fout[i].r;
        float im  = fout[i].i;
        float mag = std::sqrt(re * re + im * im) / (float)fft_size_;

        /* Convert to dB with floor at -96 dB */
        mag_db[i] = (mag > 1e-10f) ? 20.0f * std::log10(mag) : -96.0f;

        /* Track peak for pitch estimation */
        if (mag > peak_mag && i > 0) {
            peak_mag = mag;
            peak_bin = i;
        }

        /* Spectral centroid (weighted mean frequency) */
        centroid_num += (float)i * mag;
        centroid_den += mag;
    }

    /* Peak frequency with parabolic interpolation for sub-bin accuracy */
    float peak_hz = 0.0f;
    if (peak_bin > 0 && peak_bin < bins - 1) {
        float alpha = mag_db[peak_bin - 1];
        float beta  = mag_db[peak_bin];
        float gamma = mag_db[peak_bin + 1];
        float denom = alpha - 2.0f * beta + gamma;
        float p = (denom != 0.0f) ? 0.5f * (alpha - gamma) / denom : 0.0f;
        peak_hz = ((float)peak_bin + p) * bin_resolution_hz();
    }

    float centroid_hz = (centroid_den > 1e-10f)
        ? (centroid_num / centroid_den) * bin_resolution_hz()
        : 0.0f;

    /* Thread-safe update */
    {
        std::lock_guard<std::mutex> lk(spectrum_mtx_);
        magnitude_db_      = std::move(mag_db);
        peak_freq_hz_      = peak_hz;
        spectral_centroid_ = centroid_hz;
    }
}

std::vector<float> FftAnalyzer::get_magnitude_db() const
{
    std::lock_guard<std::mutex> lk(spectrum_mtx_);
    return magnitude_db_;
}

float FftAnalyzer::peak_frequency_hz() const
{
    std::lock_guard<std::mutex> lk(spectrum_mtx_);
    return peak_freq_hz_;
}

float FftAnalyzer::spectral_centroid_hz() const
{
    std::lock_guard<std::mutex> lk(spectrum_mtx_);
    return spectral_centroid_;
}

} // namespace mc1vt
