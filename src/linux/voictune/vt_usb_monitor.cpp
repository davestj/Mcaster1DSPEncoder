/*
 * Mcaster1 VoicTune — USB/BT Audio Device Hot-Plug Monitor
 * voictune/vt_usb_monitor.cpp
 *
 * Uses inotify on /dev/snd/ to detect USB audio device changes.
 * On event: waits settle_ms, re-enumerates PortAudio, fires callback.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vt_usb_monitor.h"
#include "vt_logger.h"

#include <sys/inotify.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>

#ifdef HAVE_PORTAUDIO
#include <portaudio.h>
#endif

namespace mc1vt {

UsbAudioMonitor::~UsbAudioMonitor() {
    stop();
}

void UsbAudioMonitor::start(DeviceChangeCallback cb, int settle_ms) {
    if (running_.load()) return;

    cb_         = std::move(cb);
    settle_ms_  = settle_ms;

    /* Initial enumeration */
    enumerate_usb_devices();

    inotify_fd_ = inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ < 0) {
        VT_WARN("inotify_init failed: " + std::string(strerror(errno))
                + " — USB hotplug monitoring disabled");
        return;
    }

    watch_fd_ = inotify_add_watch(inotify_fd_, "/dev/snd",
                                   IN_CREATE | IN_DELETE);
    if (watch_fd_ < 0) {
        VT_WARN("inotify_add_watch /dev/snd failed: " + std::string(strerror(errno))
                + " — USB hotplug monitoring disabled");
        close(inotify_fd_); inotify_fd_ = -1;
        return;
    }

    running_.store(true);
    monitor_thread_ = std::thread(&UsbAudioMonitor::monitor_loop, this);
    VT_INFO("USB audio hotplug monitor started (settle=" + std::to_string(settle_ms_) + "ms)");
}

void UsbAudioMonitor::stop() {
    if (!running_.load()) return;
    running_.store(false);

    if (inotify_fd_ >= 0) {
        if (watch_fd_ >= 0) inotify_rm_watch(inotify_fd_, watch_fd_);
        close(inotify_fd_);
        inotify_fd_ = -1;
        watch_fd_   = -1;
    }

    if (monitor_thread_.joinable()) monitor_thread_.join();
    VT_INFO("USB audio hotplug monitor stopped");
}

std::vector<UsbDevice> UsbAudioMonitor::list_usb_devices() const {
    std::shared_lock<std::shared_mutex> lk(devices_mtx_);
    return usb_devices_;
}

void UsbAudioMonitor::rescan() {
    enumerate_usb_devices();
    if (cb_) cb_();
}

void UsbAudioMonitor::monitor_loop() {
    const size_t BUF_LEN = 4096;
    char buf[BUF_LEN] __attribute__((aligned(__alignof__(struct inotify_event))));

    while (running_.load()) {
        struct pollfd pfd{};
        pfd.fd     = inotify_fd_;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 1000); /* 1s timeout */
        if (ret <= 0) continue;

        ssize_t len = read(inotify_fd_, buf, BUF_LEN);
        if (len <= 0) continue;

        /* Coalesce multiple events within settle window */
        bool got_event = false;
        const char* ptr = buf;
        while (ptr < buf + len) {
            const struct inotify_event* ev =
                reinterpret_cast<const struct inotify_event*>(ptr);

            if (ev->len > 0) {
                std::string name(ev->name);
                /* Only care about ALSA device nodes (pcmC*, controlC*) */
                if (name.find("pcm") == 0 || name.find("control") == 0) {
                    got_event = true;
                    const char* action = (ev->mask & IN_CREATE) ? "added" : "removed";
                    VT_INFO("Audio device " + std::string(action) + ": /dev/snd/" + name);
                }
            }
            ptr += sizeof(struct inotify_event) + ev->len;
        }

        if (got_event) {
            /* Wait for device to settle (USB audio init takes time) */
            std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms_));

            /* Drain any events that arrived during settle */
            while (read(inotify_fd_, buf, BUF_LEN) > 0) {}

            /* Re-enumerate and notify */
            enumerate_usb_devices();
            if (cb_) cb_();
        }
    }
}

void UsbAudioMonitor::enumerate_usb_devices() {
#ifdef HAVE_PORTAUDIO
    std::unique_lock<std::shared_mutex> lk(devices_mtx_);
    usb_devices_.clear();

    int n = Pa_GetDeviceCount();
    int def_in = Pa_GetDefaultInputDevice();

    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxInputChannels < 1) continue;

        std::string name = info->name ? info->name : "";

        UsbDevice dev;
        dev.pa_device_index     = i;
        dev.name                = name;
        dev.max_input_ch        = info->maxInputChannels;
        dev.default_sample_rate = static_cast<int>(info->defaultSampleRate);
        dev.is_default          = (i == def_in);
        dev.is_usb              = is_usb_device(name);
        dev.is_bluetooth        = is_bt_device(name);
        usb_devices_.push_back(std::move(dev));
    }

    VT_DBG("Enumerated " + std::to_string(usb_devices_.size()) + " input devices ("
            + std::to_string(std::count_if(usb_devices_.begin(), usb_devices_.end(),
                                           [](const UsbDevice& d) { return d.is_usb; }))
            + " USB, "
            + std::to_string(std::count_if(usb_devices_.begin(), usb_devices_.end(),
                                           [](const UsbDevice& d) { return d.is_bluetooth; }))
            + " BT)");
#else
    VT_WARN("Cannot enumerate devices — PortAudio not compiled in");
#endif
}

bool UsbAudioMonitor::is_usb_device(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find("usb") != std::string::npos
        || lower.find("yeti") != std::string::npos
        || lower.find("rode") != std::string::npos
        || lower.find("scarlett") != std::string::npos
        || lower.find("focusrite") != std::string::npos
        || lower.find("elgato") != std::string::npos
        || lower.find("shure") != std::string::npos
        || lower.find("behringer") != std::string::npos
        || lower.find("presonus") != std::string::npos
        || lower.find("samson") != std::string::npos;
}

bool UsbAudioMonitor::is_bt_device(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find("bluetooth") != std::string::npos
        || lower.find("bluez") != std::string::npos
        || lower.find("airpod") != std::string::npos
        || lower.find("jabra") != std::string::npos
        || lower.find("sony wh") != std::string::npos
        || lower.find("bose") != std::string::npos;
}

} // namespace mc1vt
