// audio_source.cpp — PortAudio live capture + CoreAudio device enumeration
// Phase 3/M6 — Mcaster1DSPEncoder
#include "audio_source.h"

#ifdef HAVE_PORTAUDIO
#include <portaudio.h>
#endif

#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
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

// ---------------------------------------------------------------------------
// CoreAudio device enumeration (macOS only)
// ---------------------------------------------------------------------------
#ifdef __APPLE__

static std::string cfstring_to_std(CFStringRef cf)
{
    if (!cf) return {};
    char buf[512];
    if (CFStringGetCString(cf, buf, sizeof(buf), kCFStringEncodingUTF8))
        return buf;
    return {};
}

static int get_channel_count(AudioObjectID dev_id, AudioObjectPropertyScope scope)
{
    AudioObjectPropertyAddress addr{};
    addr.mSelector = kAudioDevicePropertyStreamConfiguration;
    addr.mScope    = scope;
    addr.mElement  = kAudioObjectPropertyElementMain;

    UInt32 data_size = 0;
    OSStatus st = AudioObjectGetPropertyDataSize(dev_id, &addr, 0, nullptr, &data_size);
    if (st != noErr || data_size == 0) return 0;

    std::vector<uint8_t> buf(data_size);
    st = AudioObjectGetPropertyData(dev_id, &addr, 0, nullptr, &data_size, buf.data());
    if (st != noErr) return 0;

    auto *list = reinterpret_cast<AudioBufferList*>(buf.data());
    int total = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
        total += static_cast<int>(list->mBuffers[i].mNumberChannels);
    return total;
}

std::vector<CoreAudioDevice> enumerate_coreaudio_devices()
{
    std::vector<CoreAudioDevice> result;

    /* Get default input and output device IDs */
    AudioObjectPropertyAddress addr{};
    addr.mScope   = kAudioObjectPropertyScopeGlobal;
    addr.mElement = kAudioObjectPropertyElementMain;

    AudioObjectID default_input  = kAudioObjectUnknown;
    AudioObjectID default_output = kAudioObjectUnknown;

    addr.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    UInt32 sz = sizeof(default_input);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &sz, &default_input);

    addr.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    sz = sizeof(default_output);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &sz, &default_output);

    /* Get list of all audio devices */
    addr.mSelector = kAudioHardwarePropertyDevices;
    UInt32 data_size = 0;
    OSStatus st = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr,
                                                  0, nullptr, &data_size);
    if (st != noErr || data_size == 0) return result;

    int device_count = static_cast<int>(data_size / sizeof(AudioObjectID));
    std::vector<AudioObjectID> device_ids(device_count);
    st = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr,
                                     0, nullptr, &data_size, device_ids.data());
    if (st != noErr) return result;

    for (int i = 0; i < device_count; ++i) {
        AudioObjectID dev_id = device_ids[i];
        CoreAudioDevice cad{};
        cad.device_id = dev_id;

        /* Device name */
        addr.mSelector = kAudioObjectPropertyName;
        addr.mScope    = kAudioObjectPropertyScopeGlobal;
        CFStringRef name_cf = nullptr;
        sz = sizeof(name_cf);
        if (AudioObjectGetPropertyData(dev_id, &addr, 0, nullptr, &sz, &name_cf) == noErr && name_cf) {
            cad.name = cfstring_to_std(name_cf);
            CFRelease(name_cf);
        }

        /* Device UID */
        addr.mSelector = kAudioDevicePropertyDeviceUID;
        CFStringRef uid_cf = nullptr;
        sz = sizeof(uid_cf);
        if (AudioObjectGetPropertyData(dev_id, &addr, 0, nullptr, &sz, &uid_cf) == noErr && uid_cf) {
            cad.uid = cfstring_to_std(uid_cf);
            CFRelease(uid_cf);
        }

        /* Channel counts */
        cad.input_channels  = get_channel_count(dev_id, kAudioObjectPropertyScopeInput);
        cad.output_channels = get_channel_count(dev_id, kAudioObjectPropertyScopeOutput);

        /* Nominal sample rate */
        addr.mSelector = kAudioDevicePropertyNominalSampleRate;
        Float64 sr = 0;
        sz = sizeof(sr);
        if (AudioObjectGetPropertyData(dev_id, &addr, 0, nullptr, &sz, &sr) == noErr)
            cad.sample_rate = sr;

        cad.is_default_input  = (dev_id == default_input);
        cad.is_default_output = (dev_id == default_output);

        /* Skip devices with no channels at all */
        if (cad.input_channels == 0 && cad.output_channels == 0)
            continue;

        result.push_back(std::move(cad));
    }

    return result;
}

#else /* non-Apple */

std::vector<CoreAudioDevice> enumerate_coreaudio_devices()
{
    return {};
}

#endif
