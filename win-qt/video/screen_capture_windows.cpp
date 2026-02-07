/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/screen_capture_windows.cpp — DXGI Desktop Duplication stub
 *
 * Windows API: DXGI Desktop Duplication (IDXGIOutputDuplication)
 *              for low-latency GPU-accelerated screen capture.
 *              Alternative: Windows.Graphics.Capture API (Win10 1903+)
 *              for window-specific capture with yellow border.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "screen_capture_windows.h"

namespace mc1 {

ScreenCaptureSource::ScreenCaptureSource(uint32_t display_id, int w, int h, int fps)
    : display_id_(display_id)
    , width_(w)
    , height_(h)
    , fps_(fps)
{
}

ScreenCaptureSource::~ScreenCaptureSource()
{
    stop();
}

bool ScreenCaptureSource::start(VideoCallback cb)
{
    if (running_.load()) return false;

    callback_ = std::move(cb);

    // TODO: Implement with Windows API (DXGI Desktop Duplication)
    // 1. D3D11CreateDevice → ID3D11Device + ID3D11DeviceContext
    // 2. QueryInterface(IDXGIDevice) → GetAdapter → EnumOutputs(display_id_)
    // 3. IDXGIOutput1::DuplicateOutput → IDXGIOutputDuplication
    // 4. Start capture_thread_ running capture_loop()

    return false; // stub: always fails until implemented
}

void ScreenCaptureSource::stop()
{
    if (!running_.load()) return;

    running_.store(false);

    if (capture_thread_.joinable())
        capture_thread_.join();

    // TODO: Release IDXGIOutputDuplication, ID3D11Device
    duplication_ = nullptr;
    d3d_device_ = nullptr;
    callback_ = nullptr;
}

std::string ScreenCaptureSource::name() const
{
    return "Display " + std::to_string(display_id_) + " (DXGI)";
}

std::vector<ScreenInfo> ScreenCaptureSource::enumerate_displays()
{
    // TODO: Implement with Windows API
    // EnumDisplayMonitors or IDXGIFactory::EnumAdapters → EnumOutputs
    // Extract DXGI_OUTPUT_DESC for monitor name + resolution
    return {};
}

bool ScreenCaptureSource::is_available()
{
    // TODO: Implement with Windows API
    // DXGI Desktop Duplication requires Windows 8+ and a D3D11 device
    // Check via D3D11CreateDevice success
    return false;
}

void ScreenCaptureSource::capture_loop()
{
    // TODO: Implement with Windows API (DXGI Desktop Duplication)
    // Loop at fps_ rate:
    //   1. IDXGIOutputDuplication::AcquireNextFrame(timeout)
    //   2. QueryInterface(ID3D11Texture2D) on desktop resource
    //   3. Map staging texture → read BGRA pixels
    //   4. Build VideoFrame, call callback_
    //   5. ReleaseFrame()
}

} // namespace mc1
