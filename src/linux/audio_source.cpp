// audio_source.cpp — PortAudio live capture implementation
// Phase 3 — Mcaster1DSPEncoder Linux v1.2.0
#include "audio_source.h"

#ifdef HAVE_PORTAUDIO
#include <portaudio.h>
#endif

#include <cstring>
#include <stdexcept>
#include <cstdio>

// ---------------------------------------------------------------------------
// PortAudioSource — enumerate devices
// ---------------------------------------------------------------------------
std::vector<PortAudioSource::DeviceInfo> PortAudioSource::enumerate_devices()
{
    std::vector<DeviceInfo> result;

#ifdef HAVE_PORTAUDIO
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "[PortAudio] Init failed: %s\n", Pa_GetErrorText(err));
        return result;
    }

    int count = Pa_GetDeviceCount();
    int def   = Pa_GetDefaultInputDevice();

    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxInputChannels < 1) continue;

        DeviceInfo di;
        di.index               = i;
        di.name                = info->name ? info->name : "(unknown)";
        di.max_input_channels  = info->maxInputChannels;
        di.default_sample_rate = info->defaultSampleRate;
        di.is_default_input    = (i == def);
        result.push_back(di);
    }

    Pa_Terminate();
#else
    (void)0;
#endif

    return result;
}

// ---------------------------------------------------------------------------
// PortAudioSource — constructor / destructor
// ---------------------------------------------------------------------------
PortAudioSource::PortAudioSource(int device_index, int sample_rate,
                                 int channels, int frames_per_buf)
    : device_index_(device_index)
    , sample_rate_(sample_rate)
    , channels_(channels)
    , frames_per_buf_(frames_per_buf)
{
}

PortAudioSource::~PortAudioSource()
{
    stop();
}

// ---------------------------------------------------------------------------
// PortAudioSource::start
// ---------------------------------------------------------------------------
bool PortAudioSource::start(AudioCallback cb)
{
#ifdef HAVE_PORTAUDIO
    if (running_.load()) return true;

    callback_ = cb;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "[PortAudio] Pa_Initialize: %s\n", Pa_GetErrorText(err));
        return false;
    }

    // Resolve device index
    PaDeviceIndex dev = (device_index_ < 0)
                        ? Pa_GetDefaultInputDevice()
                        : static_cast<PaDeviceIndex>(device_index_);

    if (dev == paNoDevice) {
        fprintf(stderr, "[PortAudio] No input device available.\n");
        Pa_Terminate();
        return false;
    }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(dev);
    device_name_ = info ? info->name : "Unknown";

    PaStreamParameters params{};
    params.device                    = dev;
    params.channelCount              = channels_;
    params.sampleFormat              = paFloat32;
    params.suggestedLatency          = info ? info->defaultLowInputLatency : 0.05;
    params.hostApiSpecificStreamInfo = nullptr;

    PaStream* stream = nullptr;
    err = Pa_OpenStream(&stream,
                        &params,
                        nullptr,        // no output
                        static_cast<double>(sample_rate_),
                        static_cast<unsigned long>(frames_per_buf_),
                        paClipOff,
                        pa_callback_trampoline,
                        this);
    if (err != paNoError) {
        fprintf(stderr, "[PortAudio] Pa_OpenStream: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        return false;
    }

    pa_stream_ = stream;
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "[PortAudio] Pa_StartStream: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        pa_stream_ = nullptr;
        Pa_Terminate();
        return false;
    }

    running_.store(true);
    fprintf(stderr, "[PortAudio] Capture started: %s  %d Hz  %dch\n",
            device_name_.c_str(), sample_rate_, channels_);
    return true;
#else
    (void)cb;
    fprintf(stderr, "[PortAudio] Not compiled in (HAVE_PORTAUDIO not set)\n");
    return false;
#endif
}

// ---------------------------------------------------------------------------
// PortAudioSource::stop
// ---------------------------------------------------------------------------
void PortAudioSource::stop()
{
#ifdef HAVE_PORTAUDIO
    if (!running_.load()) return;
    running_.store(false);

    if (pa_stream_) {
        Pa_StopStream(static_cast<PaStream*>(pa_stream_));
        Pa_CloseStream(static_cast<PaStream*>(pa_stream_));
        pa_stream_ = nullptr;
    }
    Pa_Terminate();
    fprintf(stderr, "[PortAudio] Capture stopped.\n");
#endif
}

// ---------------------------------------------------------------------------
// PortAudioSource::pa_callback_trampoline (static) — proper PortAudio signature
// ---------------------------------------------------------------------------
#ifdef HAVE_PORTAUDIO
int PortAudioSource::pa_callback_trampoline(const void*                     input,
                                            void*                           /*output*/,
                                            unsigned long                   frame_count,
                                            const PaStreamCallbackTimeInfo* /*time_info*/,
                                            PaStreamCallbackFlags           /*status_flags*/,
                                            void*                           user_data)
{
    auto* self = static_cast<PortAudioSource*>(user_data);
    if (!self->running_.load()) return paAbort;

    if (input && self->callback_) {
        self->callback_(static_cast<const float*>(input),
                        frame_count,
                        self->channels_,
                        self->sample_rate_);
    }
    return paContinue;
}
#endif
