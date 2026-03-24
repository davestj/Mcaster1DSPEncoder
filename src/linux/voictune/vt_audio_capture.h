/*
 * Mcaster1 VoicTune — Audio Capture
 * voictune/vt_audio_capture.h
 *
 * PortAudio device enumeration and mic capture loop.
 * Pushes audio chunks to the worker pool queue for analysis.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

#ifdef HAVE_PORTAUDIO
#include <portaudio.h>
#endif

namespace mc1vt {

struct AudioDevice {
    int         index            = -1;
    std::string name;
    int         max_input_ch     = 0;
    int         max_output_ch    = 0;
    double      default_sample_rate = 48000.0;
    bool        is_default_input = false;
    bool        is_default_output = false;
};

/* Audio chunk delivered to callback */
struct AudioChunk {
    std::vector<float> samples;   /* interleaved PCM */
    int    channels    = 1;
    int    sample_rate = 48000;
    int    frames      = 0;
};

using AudioChunkCallback = std::function<void(const AudioChunk&)>;

class VtAudioCapture {
public:
    VtAudioCapture();
    ~VtAudioCapture();

    /* Initialize PortAudio (call once at startup) */
    bool init();
    void terminate();

    /* Device enumeration */
    std::vector<AudioDevice> list_devices() const;
    int  default_input_device() const;
    int  default_output_device() const;

    /* Re-enumerate devices (after hotplug event) */
    void re_enumerate();

    /* Start capturing from a device.
     * device_index = -1 for system default.
     * Calls cb on the audio thread with each chunk. */
    bool start(int device_index, int sample_rate, int channels,
               int buffer_frames, AudioChunkCallback cb);
    void stop();

    bool is_capturing() const { return capturing_.load(); }
    int  active_device() const { return active_device_index_; }

private:
    std::atomic<bool>     initialized_{false};
    std::atomic<bool>     capturing_{false};
    int                   active_device_index_ = -1;
    int                   capture_channels_    = 1;
    AudioChunkCallback    callback_;
    mutable std::mutex    devices_mtx_;
    std::vector<AudioDevice> devices_;

#ifdef HAVE_PORTAUDIO
    PaStream*             stream_ = nullptr;

    static int pa_callback(const void* input, void* output,
                           unsigned long frameCount,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void* userData);
#endif
};

} // namespace mc1vt
