/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/screen_capture_windows.cpp — DXGI Desktop Duplication screen capture
 *
 * GPU-accelerated screen capture using DXGI Desktop Duplication API.
 * Captures the desktop at the native resolution and delivers BGRA frames.
 *
 * Windows API: D3D11CreateDevice, IDXGIOutputDuplication
 * Requires: Windows 8+ (Desktop Duplication API)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <chrono>
#include <vector>

#include "screen_capture_windows.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace mc1 {

/* Internal state — kept separate from header to avoid Windows header pollution */
struct DxgiCaptureState {
    ID3D11Device         *device  = nullptr;
    ID3D11DeviceContext  *context = nullptr;
    IDXGIOutputDuplication *duplication = nullptr;
    ID3D11Texture2D      *staging = nullptr;
    DXGI_OUTPUT_DESC      output_desc{};
};

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

    auto *state = new DxgiCaptureState;

    /* Create D3D11 device */
    D3D_FEATURE_LEVEL feature_level;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, levels, 1,
        D3D11_SDK_VERSION,
        &state->device, &feature_level, &state->context);

    if (FAILED(hr)) {
        fprintf(stderr, "[DXGI] D3D11CreateDevice failed: 0x%08lx\n", hr);
        delete state;
        return false;
    }

    /* Get DXGI adapter and output */
    IDXGIDevice *dxgi_device = nullptr;
    hr = state->device->QueryInterface(__uuidof(IDXGIDevice),
                                        reinterpret_cast<void**>(&dxgi_device));
    if (FAILED(hr)) {
        fprintf(stderr, "[DXGI] QueryInterface IDXGIDevice failed\n");
        state->device->Release();
        state->context->Release();
        delete state;
        return false;
    }

    IDXGIAdapter *adapter = nullptr;
    hr = dxgi_device->GetAdapter(&adapter);
    dxgi_device->Release();

    if (FAILED(hr) || !adapter) {
        fprintf(stderr, "[DXGI] GetAdapter failed\n");
        state->device->Release();
        state->context->Release();
        delete state;
        return false;
    }

    IDXGIOutput *output = nullptr;
    hr = adapter->EnumOutputs(display_id_, &output);
    adapter->Release();

    if (FAILED(hr) || !output) {
        fprintf(stderr, "[DXGI] EnumOutputs(%u) failed — display not found\n", display_id_);
        state->device->Release();
        state->context->Release();
        delete state;
        return false;
    }

    output->GetDesc(&state->output_desc);

    /* Get IDXGIOutput1 for DuplicateOutput */
    IDXGIOutput1 *output1 = nullptr;
    hr = output->QueryInterface(__uuidof(IDXGIOutput1),
                                reinterpret_cast<void**>(&output1));
    output->Release();

    if (FAILED(hr) || !output1) {
        fprintf(stderr, "[DXGI] QueryInterface IDXGIOutput1 failed\n");
        state->device->Release();
        state->context->Release();
        delete state;
        return false;
    }

    hr = output1->DuplicateOutput(state->device, &state->duplication);
    output1->Release();

    if (FAILED(hr) || !state->duplication) {
        fprintf(stderr, "[DXGI] DuplicateOutput failed: 0x%08lx\n", hr);
        state->device->Release();
        state->context->Release();
        delete state;
        return false;
    }

    /* Get actual desktop dimensions */
    DXGI_OUTDUPL_DESC dupl_desc;
    state->duplication->GetDesc(&dupl_desc);
    width_ = static_cast<int>(dupl_desc.ModeDesc.Width);
    height_ = static_cast<int>(dupl_desc.ModeDesc.Height);

    /* Create staging texture for CPU read */
    D3D11_TEXTURE2D_DESC tex_desc{};
    tex_desc.Width = dupl_desc.ModeDesc.Width;
    tex_desc.Height = dupl_desc.ModeDesc.Height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_STAGING;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = state->device->CreateTexture2D(&tex_desc, nullptr, &state->staging);
    if (FAILED(hr)) {
        fprintf(stderr, "[DXGI] CreateTexture2D staging failed\n");
        state->duplication->Release();
        state->device->Release();
        state->context->Release();
        delete state;
        return false;
    }

    /* Store state and start capture thread */
    d3d_device_ = state->device;
    duplication_ = state;

    running_.store(true);
    capture_thread_ = std::thread(&ScreenCaptureSource::capture_loop, this);

    fprintf(stderr, "[DXGI] Screen capture started: display %u (%dx%d @ %d fps)\n",
            display_id_, width_, height_, fps_);
    return true;
}

void ScreenCaptureSource::stop()
{
    if (!running_.load()) return;

    running_.store(false);

    if (capture_thread_.joinable())
        capture_thread_.join();

    auto *state = static_cast<DxgiCaptureState*>(duplication_);
    if (state) {
        if (state->staging) state->staging->Release();
        if (state->duplication) state->duplication->Release();
        if (state->context) state->context->Release();
        if (state->device) state->device->Release();
        delete state;
    }

    duplication_ = nullptr;
    d3d_device_ = nullptr;
    callback_ = nullptr;

    fprintf(stderr, "[DXGI] Screen capture stopped\n");
}

std::string ScreenCaptureSource::name() const
{
    return "Display " + std::to_string(display_id_) + " (DXGI)";
}

std::vector<ScreenInfo> ScreenCaptureSource::enumerate_displays()
{
    std::vector<ScreenInfo> displays;

    /* Create temporary D3D11 device for enumeration */
    ID3D11Device *dev = nullptr;
    D3D_FEATURE_LEVEL fl;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, levels, 1, D3D11_SDK_VERSION,
        &dev, &fl, nullptr);

    if (FAILED(hr) || !dev) return displays;

    IDXGIDevice *dxgi_dev = nullptr;
    dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_dev));
    if (!dxgi_dev) { dev->Release(); return displays; }

    IDXGIAdapter *adapter = nullptr;
    dxgi_dev->GetAdapter(&adapter);
    dxgi_dev->Release();

    if (!adapter) { dev->Release(); return displays; }

    for (UINT i = 0; ; ++i) {
        IDXGIOutput *output = nullptr;
        if (FAILED(adapter->EnumOutputs(i, &output)) || !output) break;

        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);
        output->Release();

        ScreenInfo si;
        si.display_id = i;
        si.width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
        si.height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

        /* Convert wide device name */
        char name_buf[128]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.DeviceName, -1,
                            name_buf, sizeof(name_buf) - 1, nullptr, nullptr);
        si.name = name_buf;

        displays.push_back(std::move(si));
    }

    adapter->Release();
    dev->Release();
    return displays;
}

bool ScreenCaptureSource::is_available()
{
    /* DXGI Desktop Duplication requires Windows 8+ and a D3D11 device */
    ID3D11Device *dev = nullptr;
    D3D_FEATURE_LEVEL fl;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, levels, 1, D3D11_SDK_VERSION,
        &dev, &fl, nullptr);

    if (SUCCEEDED(hr) && dev) {
        dev->Release();
        return true;
    }
    return false;
}

void ScreenCaptureSource::capture_loop()
{
    auto *state = static_cast<DxgiCaptureState*>(duplication_);
    if (!state || !state->duplication) return;

    auto frame_interval = std::chrono::microseconds(1000000 / fps_);

    while (running_.load()) {
        auto frame_start = std::chrono::steady_clock::now();

        /* Acquire next desktop frame */
        IDXGIResource *desktop_resource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frame_info{};

        HRESULT hr = state->duplication->AcquireNextFrame(
            100, /* timeout ms */
            &frame_info,
            &desktop_resource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            /* No new frame — pace to fps */
            auto elapsed = std::chrono::steady_clock::now() - frame_start;
            if (elapsed < frame_interval) {
                auto remaining = frame_interval - elapsed;
                Sleep(static_cast<DWORD>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
            }
            continue;
        }

        if (FAILED(hr)) {
            /* Duplication lost (resolution change, secure desktop, etc.) */
            fprintf(stderr, "[DXGI] AcquireNextFrame failed: 0x%08lx\n", hr);
            Sleep(100);
            continue;
        }

        /* Get the desktop texture */
        ID3D11Texture2D *desktop_tex = nullptr;
        hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                               reinterpret_cast<void**>(&desktop_tex));
        desktop_resource->Release();

        if (FAILED(hr) || !desktop_tex) {
            state->duplication->ReleaseFrame();
            continue;
        }

        /* Copy to staging texture for CPU access */
        state->context->CopyResource(state->staging, desktop_tex);
        desktop_tex->Release();

        /* Map staging texture and read pixels */
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = state->context->Map(state->staging, 0, D3D11_MAP_READ, 0, &mapped);

        if (SUCCEEDED(hr) && callback_) {
            VideoFrame frame;
            frame.data = static_cast<const uint8_t*>(mapped.pData);
            frame.data_len = static_cast<size_t>(mapped.RowPitch) * height_;
            frame.width = width_;
            frame.height = height_;
            frame.stride = static_cast<int>(mapped.RowPitch);
            frame.pixel_format = 0; /* BGRA */

            auto now = std::chrono::steady_clock::now();
            frame.pts_us = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();

            callback_(frame);

            state->context->Unmap(state->staging, 0);
        }

        state->duplication->ReleaseFrame();

        /* Pace to target FPS */
        auto elapsed = std::chrono::steady_clock::now() - frame_start;
        if (elapsed < frame_interval) {
            auto remaining = frame_interval - elapsed;
            Sleep(static_cast<DWORD>(
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
        }
    }
}

} // namespace mc1
