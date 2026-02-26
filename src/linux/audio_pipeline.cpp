// audio_pipeline.cpp — Master audio pipeline
// Phase L5.1 — Mcaster1DSPEncoder Linux v1.4.1
#include "audio_pipeline.h"
#include "mc1_logger.h"
#include "mc1_db.h"
#include "system_health.h"
#include "server_monitors.h"
#include "../libmcaster1dspencoder/libmcaster1dspencoder.h"

#include <stdexcept>
#include <cstdio>
#include <thread>
#include <chrono>

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
    // Stop background monitors before stopping slots.
    SystemHealth::instance().stop();
    ServerMonitor::instance().stop();

    // Stop all slots
    std::lock_guard<std::mutex> lk(slots_mtx_);
    for (auto& [id, slot] : slots_) {
        if (slot) slot->stop();
    }
    slots_.clear();

    Mc1Db::instance().disconnect();
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
    // Hold slots_mtx_ only to find the slot pointer, then release before
    // launching the background thread.  This prevents the HTTP handler thread
    // from blocking during the TCP connect to DNAS (which can take seconds).
    EncoderSlot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lk(slots_mtx_);
        slot = find_slot(slot_id);
    }
    if (!slot) return false;
    // slot->start() connects to DNAS — run it in a detached thread so the
    // HTTP response returns immediately.  Browser JS polls GET /api/v1/encoders
    // for live state transitions (STARTING → CONNECTING → LIVE/ERROR).
    std::thread([slot]() { slot->start(); }).detach();
    return true;
}

bool AudioPipeline::stop_slot(int slot_id)
{
    // Also async: stop() joins the audio decode thread which may block briefly.
    EncoderSlot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lk(slots_mtx_);
        slot = find_slot(slot_id);
    }
    if (!slot) return false;
    std::thread([slot]() { slot->stop(); }).detach();
    return true;
}

bool AudioPipeline::restart_slot(int slot_id)
{
    EncoderSlot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lk(slots_mtx_);
        slot = find_slot(slot_id);
    }
    if (!slot) return false;
    std::thread([slot]() { slot->restart(); }).detach();
    return true;
}

bool AudioPipeline::wake_slot(int slot_id)
{
    EncoderSlot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lk(slots_mtx_);
        slot = find_slot(slot_id);
    }
    if (!slot) return false;
    // We wake in a detached thread — wake() calls start() which blocks on TCP connect
    std::thread([slot]() { slot->wake(); }).detach();
    return true;
}

void AudioPipeline::start_background_services()
{
    // Connect shared DB singleton (used by SystemHealth, ServerMonitor, EncoderSlot writes).
    if (!Mc1Db::instance().is_connected()) {
        if (!Mc1Db::instance().connect(gAdminConfig.db)) {
            MC1_WARN("[Pipeline] Mc1Db connect failed — metrics DB writes disabled");
        } else {
            MC1_INFO("[Pipeline] Mc1Db connected to " + std::string(gAdminConfig.db.host));
        }
    }

    // Start streaming server relay monitor.
    ServerMonitor::instance().start();
    MC1_INFO("[Pipeline] ServerMonitor started");

    // Start system health sampler (5s interval, this pipeline for slot stats).
    SystemHealth::instance().start(5, this);
    MC1_INFO("[Pipeline] SystemHealth started");
}

void AudioPipeline::start_auto_slots()
{
    // We iterate all configured slots and start those marked auto_start=true.
    // Each slot starts in its own detached thread after its configured delay.
    std::vector<std::pair<int, EncoderSlot*>> auto_slots;
    {
        std::lock_guard<std::mutex> lk(slots_mtx_);
        for (auto& [id, slot] : slots_) {
            if (slot && slot->config().auto_start)
                auto_slots.emplace_back(id, slot.get());
        }
    }

    for (auto& [id, slot] : auto_slots) {
        int delay = slot->config().auto_start_delay_sec;
        MC1_INFO("[Pipeline] Auto-start scheduled: slot " + std::to_string(id)
                 + " delay=" + std::to_string(delay) + "s");
        std::thread([slot, id, delay]() {
            if (delay > 0)
                std::this_thread::sleep_for(std::chrono::seconds(delay));
            MC1_INFO("[Pipeline] Auto-start triggered: slot " + std::to_string(id));
            slot->start();
        }).detach();
    }
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
