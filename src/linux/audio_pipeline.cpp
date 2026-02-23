// audio_pipeline.cpp — Master audio pipeline
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
#include "audio_pipeline.h"

#include <stdexcept>
#include <cstdio>

// ---------------------------------------------------------------------------
// Global pipeline instance
// ---------------------------------------------------------------------------
AudioPipeline* g_pipeline = nullptr;

// ---------------------------------------------------------------------------
// AudioPipeline — constructor / destructor
// ---------------------------------------------------------------------------
AudioPipeline::AudioPipeline() = default;

AudioPipeline::~AudioPipeline()
{
    // Stop all slots
    std::lock_guard<std::mutex> lk(slots_mtx_);
    for (auto& [id, slot] : slots_) {
        if (slot) slot->stop();
    }
    slots_.clear();
}

// ---------------------------------------------------------------------------
// add_slot
// ---------------------------------------------------------------------------
int AudioPipeline::add_slot(const EncoderConfig& cfg)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    int id = next_slot_id_++;
    EncoderConfig c = cfg;
    c.slot_id = id;
    slots_[id] = std::make_unique<EncoderSlot>(c);
    fprintf(stderr, "[Pipeline] Added slot %d: %s\n", id, c.name.c_str());
    return id;
}

// ---------------------------------------------------------------------------
// remove_slot
// ---------------------------------------------------------------------------
void AudioPipeline::remove_slot(int slot_id)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto it = slots_.find(slot_id);
    if (it == slots_.end()) return;
    if (it->second) it->second->stop();
    slots_.erase(it);
    fprintf(stderr, "[Pipeline] Removed slot %d\n", slot_id);
}

// ---------------------------------------------------------------------------
// get / update slot config
// ---------------------------------------------------------------------------
bool AudioPipeline::get_slot_config(int slot_id, EncoderConfig& out) const
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto* slot = find_slot(slot_id);
    if (!slot) return false;
    out = slot->config();
    return true;
}

bool AudioPipeline::update_slot_config(int slot_id, const EncoderConfig& cfg)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto* slot = find_slot(slot_id);
    if (!slot) return false;
    if (slot->state() != EncoderSlot::State::IDLE) return false;
    slot->update_config(cfg);
    return true;
}

int AudioPipeline::slot_count() const
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    return static_cast<int>(slots_.size());
}

// ---------------------------------------------------------------------------
// Encoder control
// ---------------------------------------------------------------------------
bool AudioPipeline::start_slot(int slot_id)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto* slot = find_slot(slot_id);
    return slot ? slot->start() : false;
}

bool AudioPipeline::stop_slot(int slot_id)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto* slot = find_slot(slot_id);
    if (!slot) return false;
    slot->stop();
    return true;
}

bool AudioPipeline::restart_slot(int slot_id)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto* slot = find_slot(slot_id);
    if (!slot) return false;
    slot->restart();
    return true;
}

bool AudioPipeline::load_playlist(int slot_id, const std::string& path)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto* slot = find_slot(slot_id);
    return slot ? slot->load_playlist(path) : false;
}

bool AudioPipeline::skip_track(int slot_id)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto* slot = find_slot(slot_id);
    if (!slot) return false;
    slot->skip_track();
    return true;
}

bool AudioPipeline::set_volume(int slot_id, float volume)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto* slot = find_slot(slot_id);
    if (!slot) return false;
    slot->set_volume(volume);
    return true;
}

bool AudioPipeline::push_metadata(int slot_id, const std::string& title,
                                   const std::string& artist)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto* slot = find_slot(slot_id);
    if (!slot) return false;
    slot->push_metadata(title, artist);
    return true;
}

bool AudioPipeline::reconfigure_dsp(int slot_id, const mc1dsp::DspChainConfig& cfg)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto* slot = find_slot(slot_id);
    if (!slot) return false;
    slot->reconfigure_dsp(cfg);
    fprintf(stderr, "[Pipeline] Slot %d DSP reconfigured: EQ=%s AGC=%s XFade=%s preset='%s'\n",
            slot_id,
            cfg.eq_enabled        ? "on" : "off",
            cfg.agc_enabled       ? "on" : "off",
            cfg.crossfader_enabled? "on" : "off",
            cfg.eq_preset.empty() ? "none" : cfg.eq_preset.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Status queries
// ---------------------------------------------------------------------------
EncoderSlot::Stats AudioPipeline::slot_stats(int slot_id) const
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto* slot = find_slot(slot_id);
    if (!slot) return EncoderSlot::Stats{};
    return slot->stats();
}

std::vector<EncoderSlot::Stats> AudioPipeline::all_stats() const
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    std::vector<EncoderSlot::Stats> result;
    result.reserve(slots_.size());
    for (auto& [id, slot] : slots_) {
        if (slot) result.push_back(slot->stats());
    }
    return result;
}

std::vector<PortAudioSource::DeviceInfo> AudioPipeline::list_devices()
{
    return PortAudioSource::enumerate_devices();
}

// ---------------------------------------------------------------------------
// Master volume
// ---------------------------------------------------------------------------
void AudioPipeline::set_master_volume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 2.0f) v = 2.0f;
    master_volume_ = v;

    // Apply to all slots
    std::lock_guard<std::mutex> lk(slots_mtx_);
    for (auto& [id, slot] : slots_) {
        if (slot) slot->set_volume(v);
    }
}

// ---------------------------------------------------------------------------
// find_slot — must hold slots_mtx_
// ---------------------------------------------------------------------------
EncoderSlot* AudioPipeline::find_slot(int slot_id) const
{
    auto it = slots_.find(slot_id);
    return (it != slots_.end()) ? it->second.get() : nullptr;
}
