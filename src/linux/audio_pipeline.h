// audio_pipeline.h — Master audio pipeline: manages all encoder slots
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
#pragma once

#include "encoder_slot.h"
#include "audio_source.h"

#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <functional>

// ---------------------------------------------------------------------------
// AudioPipeline — singleton-style manager for all encoder slots.
//
// The HTTP API handler holds a reference to this object and calls it
// to start/stop/query individual encoder slots.
// ---------------------------------------------------------------------------
class AudioPipeline {
public:
    AudioPipeline();
    ~AudioPipeline();

    // -------------------------------------------------------------------
    // Slot management
    // -------------------------------------------------------------------

    // Add a slot (returns slot id).  Configuration can be updated later.
    int  add_slot(const EncoderConfig& cfg);

    // Remove a slot (stops first if running)
    void remove_slot(int slot_id);

    // Retrieve config for a slot
    bool get_slot_config(int slot_id, EncoderConfig& out) const;

    // Update config for a slot (slot must be stopped)
    bool update_slot_config(int slot_id, const EncoderConfig& cfg);

    // Number of configured slots
    int  slot_count() const;

    // -------------------------------------------------------------------
    // Encoder control
    // -------------------------------------------------------------------

    bool start_slot  (int slot_id);
    bool stop_slot   (int slot_id);
    bool restart_slot(int slot_id);

    bool load_playlist(int slot_id, const std::string& path);
    bool skip_track   (int slot_id);
    bool set_volume   (int slot_id, float volume);
    bool push_metadata(int slot_id, const std::string& title, const std::string& artist = "");

    // Live DSP reconfiguration — takes effect on the next audio buffer
    bool reconfigure_dsp(int slot_id, const mc1dsp::DspChainConfig& cfg);

    // -------------------------------------------------------------------
    // Status queries
    // -------------------------------------------------------------------

    EncoderSlot::Stats slot_stats(int slot_id) const;
    std::vector<EncoderSlot::Stats> all_stats()  const;

    // List PortAudio input devices
    static std::vector<PortAudioSource::DeviceInfo> list_devices();

    // -------------------------------------------------------------------
    // Global volume (master fader — applied before slot-level volume)
    // -------------------------------------------------------------------
    void  set_master_volume(float v);
    float master_volume() const { return master_volume_; }

private:
    mutable std::mutex slots_mtx_;
    std::map<int, std::unique_ptr<EncoderSlot>> slots_;
    int  next_slot_id_  = 1;
    float master_volume_ = 1.0f;

    EncoderSlot* find_slot(int slot_id) const;  // must hold slots_mtx_
};

// ---------------------------------------------------------------------------
// Global pipeline instance — initialized in main_cli.cpp
// ---------------------------------------------------------------------------
extern AudioPipeline* g_pipeline;
