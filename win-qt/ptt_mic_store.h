// ptt_mic_store.h — Shared PTT mic PCM buffer between AudioPipeline and EncoderSlot
//
// The PTT mic PortAudioSource callback (owned by AudioPipeline) writes captured
// frames here.  EncoderSlot::on_audio() reads from here before calling
// DspChain::process() so that DspPttDuck can mix the mic into the stream.
//
// Using a mutex is safe: the PTT mic callback and each encoder's audio callback
// run on different threads, but the critical section (memcpy + pointer set) is
// very short (< 1 µs at typical buffer sizes).
#pragma once

#include <mutex>
#include <cstddef>
#include <cstring>

namespace mc1 {

struct PttMicStore {
    static constexpr size_t MAX_FRAMES = 4096;

    std::mutex mtx;
    float      buf[MAX_FRAMES * 2] = {};  // max stereo (2 channels)
    size_t     frames      = 0;
    int        channels    = 1;
    int        sample_rate = 44100;  // set by set_ptt_mic_device() before stream opens

    // Writes new PTT mic frames (called from PTT mic PortAudio callback thread)
    void write(const float *pcm, size_t nframes, int ch) {
        std::lock_guard<std::mutex> lk(mtx);
        size_t n = (nframes < MAX_FRAMES) ? nframes : MAX_FRAMES;
        std::memcpy(buf, pcm, n * static_cast<size_t>(ch) * sizeof(float));
        frames   = n;
        channels = ch;
    }
};

// Global instance — defined in audio_pipeline.cpp
extern PttMicStore g_ptt_mic_store;

} // namespace mc1
