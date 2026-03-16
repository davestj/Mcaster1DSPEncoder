/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * audio_pipeline.cpp — Master audio pipeline
 *
 * Ported from src/linux/audio_pipeline.cpp.
 * Removed: Mc1Db, SystemHealth, ServerMonitor (Linux-only).
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "audio_pipeline.h"
#include "ptt_mic_store.h"
#include "event_log.h"

#include <cstdio>
#include <thread>
#include <chrono>

namespace mc1 {

AudioPipeline *g_pipeline = nullptr;

/* Global PTT mic PCM store — written by PTT mic PortAudio callback,
 * read by EncoderSlot::on_audio() before each DSP chain process. */
PttMicStore g_ptt_mic_store;

AudioPipeline::AudioPipeline() = default;

AudioPipeline::~AudioPipeline()
{
    stop_dnas_poller();
    if (ptt_mic_src_) { ptt_mic_src_->stop(); ptt_mic_src_.reset(); }
    std::lock_guard<std::mutex> lk(slots_mtx_);
    for (auto &[id, slot] : slots_) {
        if (slot) slot->stop();
    }
    slots_.clear();
}

void AudioPipeline::set_ptt_mic_device(int pa_idx, int sr_override, int ch_override)
{
    /* Stop any existing PTT mic source */
    if (ptt_mic_src_) {
        ptt_mic_src_->stop();
        ptt_mic_src_.reset();
    }
    /* Clear stale buffer */
    {
        std::lock_guard<std::mutex> lk(g_ptt_mic_store.mtx);
        g_ptt_mic_store.frames = 0;
    }

    if (pa_idx < 0) {
        fprintf(stderr, "[PTT] Mic disabled\n");
        return;
    }

    /* Query device's native sample rate and channel count so the stream opens
     * successfully regardless of Windows audio driver preferences.
     * sr_override / ch_override: non-zero values override the auto-detected defaults. */
    int ptt_sr = 44100;
    int ptt_ch = 1;
    {
        auto devs = PortAudioSource::enumerate_devices();
        for (const auto &d : devs) {
            if (d.index == pa_idx) {
                ptt_sr = static_cast<int>(d.default_sample_rate);
                ptt_ch = d.max_input_channels;
                if (ptt_ch < 1) ptt_ch = 1;
                if (ptt_ch > 2) ptt_ch = 2;
                break;
            }
        }
    }
    /* Apply user overrides if provided */
    if (sr_override > 0)  ptt_sr = sr_override;
    if (ch_override > 0)  ptt_ch = ch_override;
    /* Store PTT mic sample rate in the global store so DspPttDuck can resample */
    g_ptt_mic_store.sample_rate = ptt_sr;
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "Opening PTT mic: PA index=%d  %d Hz  %dch",
                 pa_idx, ptt_sr, ptt_ch);
        fprintf(stderr, "[PTT] %s\n", buf);
        mc1::log_info("[PTT]", buf);
    }

    ptt_mic_src_ = std::make_unique<PortAudioSource>(pa_idx, ptt_sr, ptt_ch, 512);
    bool ok = ptt_mic_src_->start(
        [](const float *pcm, size_t frames, int ch, int /*sr*/) {
            g_ptt_mic_store.write(pcm, frames, ch);
        });
    if (!ok) {
        ptt_mic_src_.reset();
        char buf[160];
        snprintf(buf, sizeof(buf), "FAILED to open PTT mic PA device %d — check device in PTT combo", pa_idx);
        fprintf(stderr, "[PTT] %s\n", buf);
        mc1::log_error("[PTT]", buf);
    } else {
        char buf[160];
        snprintf(buf, sizeof(buf), "PTT mic ready: PA device %d  %d Hz  %dch", pa_idx, ptt_sr, ptt_ch);
        fprintf(stderr, "[PTT] %s\n", buf);
        mc1::log_connect("[PTT]", buf);
    }
}

int AudioPipeline::add_slot(const EncoderConfig &cfg)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    int id = next_slot_id_++;
    EncoderConfig c = cfg;
    c.slot_id = id;
    slots_[id] = std::make_unique<EncoderSlot>(c);
    if (dnas_poller_)
        dnas_poller_->register_slot(id, c.stream_target);
    fprintf(stderr, "[Pipeline] Added slot %d: %s\n", id, c.name.c_str());
    return id;
}

void AudioPipeline::remove_slot(int slot_id)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto it = slots_.find(slot_id);
    if (it == slots_.end()) return;
    if (it->second) it->second->stop();
    if (dnas_poller_)
        dnas_poller_->unregister_slot(slot_id);
    slots_.erase(it);
    fprintf(stderr, "[Pipeline] Removed slot %d\n", slot_id);
}

bool AudioPipeline::get_slot_config(int slot_id, EncoderConfig &out) const
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto *slot = find_slot(slot_id);
    if (!slot) return false;
    out = slot->config();
    return true;
}

bool AudioPipeline::update_slot_config(int slot_id, const EncoderConfig &cfg)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto *slot = find_slot(slot_id);
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

bool AudioPipeline::start_slot(int slot_id)
{
    EncoderSlot *slot = nullptr;
    {
        std::lock_guard<std::mutex> lk(slots_mtx_);
        slot = find_slot(slot_id);
    }
    if (!slot) return false;
    std::thread([slot]() { slot->start(); }).detach();
    return true;
}

bool AudioPipeline::stop_slot(int slot_id)
{
    EncoderSlot *slot = nullptr;
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
    EncoderSlot *slot = nullptr;
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
    EncoderSlot *slot = nullptr;
    {
        std::lock_guard<std::mutex> lk(slots_mtx_);
        slot = find_slot(slot_id);
    }
    if (!slot) return false;
    std::thread([slot]() { slot->wake(); }).detach();
    return true;
}

void AudioPipeline::start_auto_slots()
{
    std::vector<std::pair<int, EncoderSlot *>> auto_slots;
    {
        std::lock_guard<std::mutex> lk(slots_mtx_);
        for (auto &[id, slot] : slots_) {
            if (slot && slot->config().auto_start)
                auto_slots.emplace_back(id, slot.get());
        }
    }

    for (auto &p : auto_slots) {
        int sid = p.first;
        EncoderSlot *slot = p.second;
        int delay = slot->config().auto_start_delay_sec;
        fprintf(stderr, "[Pipeline] Auto-start scheduled: slot %d delay=%ds\n", sid, delay);
        std::thread([slot, sid, delay]() {
            if (delay > 0)
                std::this_thread::sleep_for(std::chrono::seconds(delay));
            fprintf(stderr, "[Pipeline] Auto-start triggered: slot %d\n", sid);
            slot->start();
        }).detach();
    }
}

bool AudioPipeline::load_playlist(int slot_id, const std::string &path)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto *slot = find_slot(slot_id);
    return slot ? slot->load_playlist(path) : false;
}

bool AudioPipeline::skip_track(int slot_id)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto *slot = find_slot(slot_id);
    if (!slot) return false;
    slot->skip_track();
    return true;
}

bool AudioPipeline::set_volume(int slot_id, float volume)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto *slot = find_slot(slot_id);
    if (!slot) return false;
    slot->set_volume(volume);
    return true;
}

bool AudioPipeline::push_metadata(int slot_id, const std::string &title,
                                   const std::string &artist)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto *slot = find_slot(slot_id);
    if (!slot) return false;
    slot->push_metadata(title, artist);
    return true;
}

bool AudioPipeline::reconfigure_dsp(int slot_id, const mc1dsp::DspChainConfig &cfg)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto *slot = find_slot(slot_id);
    if (!slot) return false;
    slot->reconfigure_dsp(cfg);
    fprintf(stderr, "[Pipeline] Slot %d DSP reconfigured: EQ=%s AGC=%s XFade=%s preset='%s'\n",
            slot_id,
            cfg.eq_enabled         ? "on" : "off",
            cfg.agc_enabled        ? "on" : "off",
            cfg.crossfader_enabled ? "on" : "off",
            cfg.eq_preset.empty()  ? "none" : cfg.eq_preset.c_str());
    return true;
}

mc1dsp::DspChain& AudioPipeline::dsp_chain(int slot_id)
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto *slot = find_slot(slot_id);
    if (!slot) {
        /* Fallback: return first slot's chain or a static dummy */
        static mc1dsp::DspChain dummy;
        if (!slots_.empty()) return slots_.begin()->second->dsp_chain();
        return dummy;
    }
    return slot->dsp_chain();
}

EncoderSlot::Stats AudioPipeline::slot_stats(int slot_id) const
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    auto *slot = find_slot(slot_id);
    if (!slot) return EncoderSlot::Stats{};
    return slot->stats();
}

std::vector<EncoderSlot::Stats> AudioPipeline::all_stats() const
{
    std::lock_guard<std::mutex> lk(slots_mtx_);
    std::vector<EncoderSlot::Stats> result;
    result.reserve(slots_.size());
    for (auto &[id, slot] : slots_) {
        if (!slot) continue;
        auto st = slot->stats();
        // Merge per-slot DNAS stats from the poller
        if (dnas_poller_) {
            auto ms = dnas_poller_->get_mount_stats(id);
            st.listeners     = ms.listeners;
            st.listener_peak = ms.listener_peak;
            st.out_kbps      = ms.out_kbps;
        }
        result.push_back(st);
    }
    return result;
}

std::vector<PortAudioSource::DeviceInfo> AudioPipeline::list_devices()
{
    return PortAudioSource::enumerate_devices();
}

std::vector<CameraDeviceInfo> AudioPipeline::list_cameras()
{
    return CameraSource::enumerate_devices();
}

void AudioPipeline::set_master_volume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 2.0f) v = 2.0f;
    master_volume_ = v;

    std::lock_guard<std::mutex> lk(slots_mtx_);
    for (auto &[id, slot] : slots_) {
        if (slot) slot->set_volume(v);
    }
}

// ---------------------------------------------------------------------------
// DNAS poller management
// ---------------------------------------------------------------------------
void AudioPipeline::start_dnas_poller(int interval_sec)
{
    if (!dnas_poller_)
        dnas_poller_ = std::make_unique<DnasSlotPoller>();

    // Register all existing slots
    std::lock_guard<std::mutex> lk(slots_mtx_);
    for (auto &[id, slot] : slots_) {
        if (slot)
            dnas_poller_->register_slot(id, slot->config().stream_target);
    }
    dnas_poller_->start(interval_sec);
    fprintf(stderr, "[Pipeline] DNAS slot poller started (interval=%ds)\n", interval_sec);
}

void AudioPipeline::stop_dnas_poller()
{
    if (dnas_poller_) {
        dnas_poller_->stop();
        fprintf(stderr, "[Pipeline] DNAS slot poller stopped\n");
    }
}

EncoderSlot *AudioPipeline::find_slot(int slot_id) const
{
    auto it = slots_.find(slot_id);
    return (it != slots_.end()) ? it->second.get() : nullptr;
}

} // namespace mc1
