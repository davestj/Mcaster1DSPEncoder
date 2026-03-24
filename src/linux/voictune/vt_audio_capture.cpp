/*
 * Mcaster1 VoicTune — Audio Capture
 * voictune/vt_audio_capture.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vt_audio_capture.h"
#include "vt_logger.h"

namespace mc1vt {

VtAudioCapture::VtAudioCapture() = default;

VtAudioCapture::~VtAudioCapture() {
    stop();
    terminate();
}

bool VtAudioCapture::init() {
#ifdef HAVE_PORTAUDIO
    if (initialized_.load()) return true;
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        VT_ERR("Pa_Initialize failed: " + std::string(Pa_GetErrorText(err)));
        return false;
    }
    initialized_.store(true);
    re_enumerate();
    VT_INFO("PortAudio initialized, " + std::to_string(devices_.size()) + " devices found");
    return true;
#else
    VT_WARN("PortAudio not available (compiled without HAVE_PORTAUDIO)");
    return false;
#endif
}

void VtAudioCapture::terminate() {
#ifdef HAVE_PORTAUDIO
    if (!initialized_.load()) return;
    stop();
    Pa_Terminate();
    initialized_.store(false);
    VT_INFO("PortAudio terminated");
#endif
}

std::vector<AudioDevice> VtAudioCapture::list_devices() const {
    std::lock_guard<std::mutex> lk(devices_mtx_);
    return devices_;
}

int VtAudioCapture::default_input_device() const {
#ifdef HAVE_PORTAUDIO
    if (!initialized_.load()) return -1;
    return Pa_GetDefaultInputDevice();
#else
    return -1;
#endif
}

int VtAudioCapture::default_output_device() const {
#ifdef HAVE_PORTAUDIO
    if (!initialized_.load()) return -1;
    return Pa_GetDefaultOutputDevice();
#else
    return -1;
#endif
}

void VtAudioCapture::re_enumerate() {
#ifdef HAVE_PORTAUDIO
    if (!initialized_.load()) return;

    std::lock_guard<std::mutex> lk(devices_mtx_);
    devices_.clear();

    int n = Pa_GetDeviceCount();
    int def_in  = Pa_GetDefaultInputDevice();
    int def_out = Pa_GetDefaultOutputDevice();

    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) continue;

        AudioDevice d;
        d.index              = i;
        d.name               = info->name ? info->name : "(unknown)";
        d.max_input_ch       = info->maxInputChannels;
        d.max_output_ch      = info->maxOutputChannels;
        d.default_sample_rate = info->defaultSampleRate;
        d.is_default_input   = (i == def_in);
        d.is_default_output  = (i == def_out);
        devices_.push_back(std::move(d));
    }

    VT_DBG("Device re-enumeration: " + std::to_string(devices_.size()) + " devices");
#endif
}

bool VtAudioCapture::start(int device_index, int sample_rate, int channels,
                            int buffer_frames, AudioChunkCallback cb) {
#ifdef HAVE_PORTAUDIO
    if (!initialized_.load()) {
        VT_ERR("Cannot start capture — PortAudio not initialized");
        return false;
    }
    if (capturing_.load()) {
        VT_WARN("Already capturing, stopping first");
        stop();
    }

    callback_ = std::move(cb);

    PaStreamParameters params;
    params.device = (device_index >= 0) ? device_index : Pa_GetDefaultInputDevice();
    if (params.device == paNoDevice) {
        VT_ERR("No input device available");
        return false;
    }

    const PaDeviceInfo* dev_info = Pa_GetDeviceInfo(params.device);
    if (!dev_info) {
        VT_ERR("Invalid device index " + std::to_string(params.device));
        return false;
    }

    int ch = channels;
    if (ch > dev_info->maxInputChannels) ch = dev_info->maxInputChannels;
    if (ch < 1) ch = 1;

    params.channelCount              = ch;
    params.sampleFormat              = paFloat32;
    params.suggestedLatency          = dev_info->defaultLowInputLatency;
    params.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(&stream_, &params, nullptr,
                                sample_rate, buffer_frames, paClipOff,
                                pa_callback, this);
    if (err != paNoError) {
        VT_ERR("Pa_OpenStream failed: " + std::string(Pa_GetErrorText(err)));
        stream_ = nullptr;
        return false;
    }

    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        VT_ERR("Pa_StartStream failed: " + std::string(Pa_GetErrorText(err)));
        Pa_CloseStream(stream_);
        stream_ = nullptr;
        return false;
    }

    active_device_index_ = params.device;
    capture_channels_    = ch;
    capturing_.store(true);
    VT_INFO("Capturing from device " + std::to_string(params.device)
            + " [" + std::string(dev_info->name) + "] "
            + std::to_string(sample_rate) + "Hz " + std::to_string(ch) + "ch");
    return true;
#else
    (void)device_index; (void)sample_rate; (void)channels;
    (void)buffer_frames; (void)cb;
    VT_ERR("PortAudio not compiled in");
    return false;
#endif
}

void VtAudioCapture::stop() {
#ifdef HAVE_PORTAUDIO
    if (!capturing_.load()) return;
    capturing_.store(false);
    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }
    active_device_index_ = -1;
    VT_INFO("Audio capture stopped");
#endif
}

#ifdef HAVE_PORTAUDIO
int VtAudioCapture::pa_callback(const void* input, void* /*output*/,
                                 unsigned long frameCount,
                                 const PaStreamCallbackTimeInfo* /*timeInfo*/,
                                 PaStreamCallbackFlags /*statusFlags*/,
                                 void* userData) {
    auto* self = static_cast<VtAudioCapture*>(userData);
    if (!self->capturing_.load() || !self->callback_ || !input)
        return paContinue;

    const float* in = static_cast<const float*>(input);
    const PaStreamInfo* si = Pa_GetStreamInfo(self->stream_);

    AudioChunk chunk;
    chunk.frames      = static_cast<int>(frameCount);
    chunk.sample_rate = si ? static_cast<int>(si->sampleRate) : 48000;
    chunk.channels    = self->capture_channels_;
    chunk.samples.assign(in, in + frameCount * chunk.channels);

    self->callback_(chunk);
    return paContinue;
}
#endif

} // namespace mc1vt
