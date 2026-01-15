// audio_source.h — Abstract audio source interface + PortAudio live capture backend
// Phase 3 — Mcaster1DSPEncoder Linux v1.2.0
#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

// ---------------------------------------------------------------------------
// AudioCallback — called by source for each buffer of PCM audio.
//   pcm         : interleaved float32 samples in range [-1.0, +1.0]
//   frames      : number of sample frames (= samples / channels)
//   channels    : 1 = mono, 2 = stereo
//   sample_rate : e.g. 44100, 48000
// ---------------------------------------------------------------------------
using AudioCallback = std::function<void(const float* pcm,
                                         size_t frames,
                                         int    channels,
                                         int    sample_rate)>;

// ---------------------------------------------------------------------------
// AudioSource — pure virtual base class
// ---------------------------------------------------------------------------
class AudioSource {
public:
    virtual ~AudioSource() = default;

    // Start producing audio; cb is called from an audio thread.
    virtual bool start(AudioCallback cb) = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;

    virtual int  sample_rate() const = 0;
    virtual int  channels()    const = 0;
    virtual std::string name() const = 0;
};

#ifdef HAVE_PORTAUDIO
#include <portaudio.h>
#endif

// ---------------------------------------------------------------------------
// PortAudioSource — live device capture via PortAudio
// ---------------------------------------------------------------------------
class PortAudioSource : public AudioSource {
public:
    struct DeviceInfo {
        int         index;
        std::string name;
        int         max_input_channels;
        double      default_sample_rate;
        bool        is_default_input;
    };

    // Enumerate all available PortAudio input devices.
    // Returns empty vector if PortAudio fails to initialize.
    static std::vector<DeviceInfo> enumerate_devices();

    // device_index : PortAudio device index, or -1 for system default input
    // sample_rate  : desired capture rate (44100 or 48000 recommended)
    // channels     : 1 (mono) or 2 (stereo)
    // frames_per_buf : callback buffer size in frames (0 = PortAudio chooses)
    explicit PortAudioSource(int device_index   = -1,
                             int sample_rate    = 44100,
                             int channels       = 2,
                             int frames_per_buf = 1024);
    ~PortAudioSource() override;

    bool start(AudioCallback cb) override;
    void stop()                  override;
    bool is_running() const      override { return running_.load(); }

    int         sample_rate() const override { return sample_rate_;   }
    int         channels()    const override { return channels_;       }
    std::string name()        const override { return device_name_;   }

private:
    int         device_index_;
    int         sample_rate_;
    int         channels_;
    int         frames_per_buf_;
    std::string device_name_;

#ifdef HAVE_PORTAUDIO
    PaStream*      pa_stream_  = nullptr;
#else
    void*          pa_stream_  = nullptr;
#endif
    AudioCallback  callback_;
    std::atomic<bool> running_{false};

#ifdef HAVE_PORTAUDIO
    // Proper PortAudio callback signature
    static int pa_callback_trampoline(const void*                     input,
                                      void*                           output,
                                      unsigned long                   frame_count,
                                      const PaStreamCallbackTimeInfo* time_info,
                                      PaStreamCallbackFlags           status_flags,
                                      void*                           user_data);
#endif
};
