/*
 * Mcaster1 VoicTune — USB/BT Audio Device Hot-Plug Monitor
 * voictune/vt_usb_monitor.h
 *
 * Monitors /dev/snd/ via inotify for USB mic hotplug events.
 * Optionally monitors PulseAudio for Bluetooth audio devices.
 * On device change: re-enumerates PortAudio, notifies via callback.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>

namespace mc1vt {

struct UsbDevice {
    int         pa_device_index     = -1;
    std::string name;
    std::string usb_id;             /* "VID:PID" e.g. "b58e:9e84" */
    int         max_input_ch        = 0;
    int         default_sample_rate = 48000;
    bool        is_default          = false;
    bool        is_usb              = false;
    bool        is_bluetooth        = false;
};

class UsbAudioMonitor {
public:
    using DeviceChangeCallback = std::function<void()>;

    UsbAudioMonitor() = default;
    ~UsbAudioMonitor();

    /* Start inotify monitoring thread.
     * settle_ms: delay after event before re-enumeration (device init time).
     * cb: called on device add/remove (from monitor thread). */
    void start(DeviceChangeCallback cb, int settle_ms = 500);
    void stop();

    bool is_monitoring() const { return running_.load(); }

    /* Current device list (populated on each change event) */
    std::vector<UsbDevice> list_usb_devices() const;

    /* Force re-scan (called externally or after hotplug event) */
    void rescan();

private:
    std::thread              monitor_thread_;
    std::atomic<bool>        running_{false};
    int                      inotify_fd_ = -1;
    int                      watch_fd_   = -1;
    int                      settle_ms_  = 500;

    mutable std::shared_mutex devices_mtx_;
    std::vector<UsbDevice>    usb_devices_;

    DeviceChangeCallback     cb_;

    void monitor_loop();
    void enumerate_usb_devices();

    /* Check if a PortAudio device name suggests USB or Bluetooth */
    static bool is_usb_device(const std::string& name);
    static bool is_bt_device(const std::string& name);
};

} // namespace mc1vt
