/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * audio_pipeline.h — Master audio pipeline: manages all encoder slots
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_AUDIO_PIPELINE_H
#define MC1_AUDIO_PIPELINE_H

#include "encoder_slot.h"
#include "audio_source.h"
#include "dnas_slot_poller.h"
#include "dsp/dsp_chain.h"
#ifdef _WIN32
#include "video/video_capture_windows.h"
#else
#include "video/video_capture_macos.h"
#endif

#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <functional>

namespace mc1 {

class AudioPipeline {
public:
    AudioPipeline();
    ~AudioPipeline();

    /* Slot management */
    int  add_slot(const EncoderConfig &cfg);
    void remove_slot(int slot_id);
    bool get_slot_config(int slot_id, EncoderConfig &out) const;
    bool update_slot_config(int slot_id, const EncoderConfig &cfg);
    int  slot_count() const;

    /* Encoder control */
    bool start_slot(int slot_id);
    bool stop_slot(int slot_id);
    bool restart_slot(int slot_id);
    bool wake_slot(int slot_id);

    void start_auto_slots();

    bool load_playlist(int slot_id, const std::string &path);
    bool skip_track(int slot_id);
    bool set_volume(int slot_id, float volume);
    bool push_metadata(int slot_id, const std::string &title,
                       const std::string &artist = "");

    bool reconfigure_dsp(int slot_id, const mc1dsp::DspChainConfig &cfg);

    /* Phase M6: Direct access to slot's DSP chain (for 31-band EQ / Sonic Enhancer) */
    mc1dsp::DspChain& dsp_chain(int slot_id);

    /* Status queries */
    EncoderSlot::Stats slot_stats(int slot_id) const;
    std::vector<EncoderSlot::Stats> all_stats() const;

    static std::vector<PortAudioSource::DeviceInfo> list_devices();
    static std::vector<CameraDeviceInfo> list_cameras();

    /* Master volume */
    void  set_master_volume(float v);
    float master_volume() const { return master_volume_; }

    /* Phase M5: per-slot DNAS stats polling */
    void start_dnas_poller(int interval_sec = 15);
    void stop_dnas_poller();
    DnasSlotPoller *dnas_poller() { return dnas_poller_.get(); }

    /* Phase M8: iterate over all slots (lock held internally) */
    template <typename Fn>
    void for_each_slot(Fn &&fn) const {
        std::lock_guard<std::mutex> lk(slots_mtx_);
        for (auto &[id, slot] : slots_)
            fn(id, *slot);
    }

private:
    mutable std::mutex slots_mtx_;
    std::map<int, std::unique_ptr<EncoderSlot>> slots_;
    int   next_slot_id_  = 1;
    float master_volume_ = 1.0f;

    std::unique_ptr<DnasSlotPoller> dnas_poller_;

    EncoderSlot *find_slot(int slot_id) const;
};

/* Global pipeline instance — set in main.cpp */
extern AudioPipeline *g_pipeline;

} // namespace mc1

#endif // MC1_AUDIO_PIPELINE_H
