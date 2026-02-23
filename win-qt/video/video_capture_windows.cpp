/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/video_capture_windows.cpp — Media Foundation camera capture
 *
 * Windows API: Media Foundation (MFEnumDeviceSources, IMFSourceReader
 *              in synchronous mode for webcam capture → RGB32 frames)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "video_capture_windows.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")

/* DirectShow — for enumerating virtual cameras not visible to MF */
#include <dshow.h>
#pragma comment(lib, "strmiids.lib")

#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

/* Helper: convert WCHAR* to std::string (UTF-8) */
static std::string wchar_to_utf8(const WCHAR *wstr)
{
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &out[0], len, nullptr, nullptr);
    return out;
}

namespace mc1 {

CameraSource::CameraSource(int device_index, int width, int height, int fps)
    : device_index_(device_index)
    , width_(width)
    , height_(height)
    , fps_(fps)
    , device_name_("Camera " + std::to_string(device_index))
{
}

CameraSource::~CameraSource()
{
    stop();
}

bool CameraSource::start(VideoCallback cb)
{
    if (running_.load()) return false;

    callback_ = std::move(cb);

    /* ── 1. Start Media Foundation ────────────────────────────────── */
    /* Ensure COM is initialized on this thread (idempotent) */
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        fprintf(stderr, "[VideoCapture] MFStartup failed: 0x%08lX\n", hr);
        return false;
    }
    mf_started_ = true;

    /* ── 2. Enumerate devices, find the one at device_index_ ─────── */
    IMFAttributes *pConfig = nullptr;
    hr = MFCreateAttributes(&pConfig, 1);
    if (FAILED(hr)) { stop(); return false; }

    pConfig->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                     MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate **ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pConfig, &ppDevices, &count);
    pConfig->Release();

    if (FAILED(hr) || count == 0) {
        fprintf(stderr, "[VideoCapture] No video capture devices found\n");
        if (ppDevices) CoTaskMemFree(ppDevices);
        stop();
        return false;
    }

    if (device_index_ < 0 || static_cast<UINT32>(device_index_) >= count) {
        fprintf(stderr, "[VideoCapture] device_index %d out of range (0..%u)\n",
                device_index_, count - 1);
        for (UINT32 i = 0; i < count; ++i) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);
        stop();
        return false;
    }

    /* Read the friendly name for status messages */
    {
        WCHAR *name = nullptr; UINT32 name_len = 0;
        if (SUCCEEDED(ppDevices[device_index_]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &name_len))) {
            device_name_ = wchar_to_utf8(name);
            CoTaskMemFree(name);
        }
    }

    /* ── 3. Activate → IMFMediaSource ────────────────────────────── */
    IMFMediaSource *pSource = nullptr;
    hr = ppDevices[device_index_]->ActivateObject(
            IID_PPV_ARGS(&pSource));

    /* Release all activate objects */
    for (UINT32 i = 0; i < count; ++i) ppDevices[i]->Release();
    CoTaskMemFree(ppDevices);

    if (FAILED(hr) || !pSource) {
        fprintf(stderr, "[VideoCapture] ActivateObject failed: 0x%08lX\n", hr);
        stop();
        return false;
    }
    media_source_ = pSource;

    /* ── 4. Create IMFSourceReader with video processing enabled ── */
    /* MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING lets the source reader
     * insert a color converter (e.g. YUY2/NV12/MJPG → RGB32).
     * Without this, SetCurrentMediaType(RGB32) fails on cameras
     * that only output YUY2/NV12 natively. */
    IMFAttributes *pReaderAttr = nullptr;
    MFCreateAttributes(&pReaderAttr, 1);
    if (pReaderAttr)
        pReaderAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    IMFSourceReader *pReader = nullptr;
    hr = MFCreateSourceReaderFromMediaSource(pSource, pReaderAttr, &pReader);
    if (pReaderAttr) pReaderAttr->Release();
    if (FAILED(hr) || !pReader) {
        fprintf(stderr, "[VideoCapture] MFCreateSourceReaderFromMediaSource failed: 0x%08lX\n", hr);
        stop();
        return false;
    }
    source_reader_ = pReader;

    /* ── 5. Set output type — try RGB32 first, fall back to NV12 ── */
    /* On macOS/AVFoundation, cameras natively output BGRA. On Windows,
     * most USB webcams output YUY2 or NV12. The MF source reader can
     * convert to RGB32 if MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING is
     * set, but this doesn't work on all systems/cameras. We try RGB32
     * first, then NV12, then accept the camera's default format. */
    capture_fmt_ = 0; /* default: RGB32 */
    {
        struct { GUID guid; int fmt_id; const char *name; } formats_to_try[] = {
            { MFVideoFormat_RGB32, 0, "RGB32" },
            { MFVideoFormat_NV12,  1, "NV12"  },
            { MFVideoFormat_YUY2,  2, "YUY2"  },
        };
        bool format_set = false;
        for (const auto &f : formats_to_try) {
            IMFMediaType *pOutType = nullptr;
            MFCreateMediaType(&pOutType);
            pOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pOutType->SetGUID(MF_MT_SUBTYPE, f.guid);
            hr = pReader->SetCurrentMediaType(
                    MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pOutType);
            pOutType->Release();
            if (SUCCEEDED(hr)) {
                capture_fmt_ = f.fmt_id;
                format_set = true;
                fprintf(stderr, "[VideoCapture] Using format: %s\n", f.name);
                break;
            }
        }
        if (!format_set) {
            fprintf(stderr, "[VideoCapture] No supported output format found\n");
            stop();
            return false;
        }
    }

    /* ── 6. Read back actual output dimensions + stride ───────────── */
    IMFMediaType *pActual = nullptr;
    hr = pReader->GetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pActual);
    if (SUCCEEDED(hr) && pActual) {
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(pActual, MF_MT_FRAME_SIZE, &w, &h);
        if (w > 0 && h > 0) {
            width_  = static_cast<int>(w);
            height_ = static_cast<int>(h);
        }

        /* MF_MT_DEFAULT_STRIDE: positive = top-down, negative = bottom-up.
         * IMFAttributes has GetUINT32 — reinterpret as signed for stride. */
        UINT32 raw_stride = 0;
        if (SUCCEEDED(pActual->GetUINT32(MF_MT_DEFAULT_STRIDE, &raw_stride))) {
            INT32 default_stride = static_cast<INT32>(raw_stride);
            stride_ = static_cast<int>(default_stride);
        } else {
            /* Compute stride from format and width */
            if (capture_fmt_ == 0)      stride_ = width_ * 4;   /* RGB32 */
            else if (capture_fmt_ == 1) stride_ = width_;        /* NV12 Y-plane */
            else if (capture_fmt_ == 2) stride_ = width_ * 2;   /* YUY2 */
            else                        stride_ = width_ * 4;
        }
        bottom_up_ = (stride_ < 0);
        if (stride_ < 0) stride_ = -stride_;

        fprintf(stderr, "[VideoCapture] Stride: %d bytes/row, bottom-up: %s\n",
                stride_, bottom_up_ ? "yes" : "no");

        pActual->Release();
    } else {
        stride_ = width_ * 4;
        bottom_up_ = false;
    }

    fprintf(stderr, "[VideoCapture] Started: '%s' %dx%d @ %d fps\n",
            device_name_.c_str(), width_, height_, fps_);

    /* ── 7. Start capture thread ─────────────────────────────────── */
    running_.store(true);
    capture_thread_ = std::thread(&CameraSource::capture_loop, this);

    return true;
}

void CameraSource::stop()
{
    running_.store(false);

    if (capture_thread_.joinable())
        capture_thread_.join();

    if (source_reader_) {
        static_cast<IMFSourceReader*>(source_reader_)->Release();
        source_reader_ = nullptr;
    }

    if (media_source_) {
        auto *pSource = static_cast<IMFMediaSource*>(media_source_);
        pSource->Shutdown();
        pSource->Release();
        media_source_ = nullptr;
    }

    if (mf_started_) {
        MFShutdown();
        mf_started_ = false;
    }

    callback_ = nullptr;
}

std::vector<CameraDeviceInfo> CameraSource::enumerate_devices()
{
    std::vector<CameraDeviceInfo> result;

    /* COM must be initialised on the calling thread */
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool    comInited = SUCCEEDED(hrCom) || hrCom == S_FALSE; /* S_FALSE = already init */

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
        if (comInited) CoUninitialize();
        return result;
    }

    IMFAttributes *pConfig = nullptr;
    if (FAILED(MFCreateAttributes(&pConfig, 1))) {
        MFShutdown();
        if (comInited) CoUninitialize();
        return result;
    }

    pConfig->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                     MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate **ppDevices = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(pConfig, &ppDevices, &count);
    pConfig->Release();

    fprintf(stderr, "[VideoCapture] MFEnumDeviceSources found %u device(s)\n", count);

    if (SUCCEEDED(hr)) {
        for (UINT32 i = 0; i < count; ++i) {
            CameraDeviceInfo info{};
            info.index    = static_cast<int>(i);
            info.is_front = false;

            /* Friendly name */
            WCHAR *name = nullptr;
            UINT32 name_len = 0;
            if (SUCCEEDED(ppDevices[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &name_len))) {
                info.name = wchar_to_utf8(name);
                CoTaskMemFree(name);
            }
            if (info.name.empty())
                info.name = "Camera " + std::to_string(i);

            /* Symbolic link (unique ID) */
            WCHAR *link = nullptr;
            UINT32 link_len = 0;
            if (SUCCEEDED(ppDevices[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                    &link, &link_len))) {
                info.unique_id = wchar_to_utf8(link);
                CoTaskMemFree(link);
            }
            if (info.unique_id.empty())
                info.unique_id = "camera:" + std::to_string(i);

            fprintf(stderr, "[VideoCapture]   device[%u]: '%s'\n", i, info.name.c_str());
            result.push_back(std::move(info));
            ppDevices[i]->Release();
        }
        CoTaskMemFree(ppDevices);
    }

    MFShutdown();

    /* ── DirectShow fallback: find virtual cameras not in MF ─────── */
    /* Virtual cameras (OBS, Mcaster1 Virtual Camera, etc.) register
     * as DirectShow filters but NOT as MF sources. Enumerate them
     * via ICreateDevEnum and add any not already found by MF. */
    {
        std::set<std::string> mf_names;
        for (const auto &d : result)
            mf_names.insert(d.name);

        ICreateDevEnum *pDevEnum = nullptr;
        hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDevEnum));
        if (SUCCEEDED(hr) && pDevEnum) {
            IEnumMoniker *pEnum = nullptr;
            hr = pDevEnum->CreateClassEnumerator(
                    CLSID_VideoInputDeviceCategory, &pEnum, 0);
            if (hr == S_OK && pEnum) {
                IMoniker *pMoniker = nullptr;
                while (pEnum->Next(1, &pMoniker, nullptr) == S_OK) {
                    IPropertyBag *pBag = nullptr;
                    hr = pMoniker->BindToStorage(nullptr, nullptr,
                                                  IID_PPV_ARGS(&pBag));
                    if (SUCCEEDED(hr) && pBag) {
                        VARIANT var;
                        VariantInit(&var);
                        hr = pBag->Read(L"FriendlyName", &var, nullptr);
                        if (SUCCEEDED(hr)) {
                            std::string dshow_name = wchar_to_utf8(var.bstrVal);
                            /* Only add if NOT already found by MF */
                            if (!dshow_name.empty() &&
                                mf_names.find(dshow_name) == mf_names.end()) {
                                CameraDeviceInfo info{};
                                info.index = static_cast<int>(result.size());
                                info.name  = dshow_name;
                                info.is_front = false;

                                /* Try to get DevicePath as unique ID */
                                VARIANT varPath;
                                VariantInit(&varPath);
                                if (SUCCEEDED(pBag->Read(L"DevicePath", &varPath, nullptr))) {
                                    info.unique_id = wchar_to_utf8(varPath.bstrVal);
                                    VariantClear(&varPath);
                                }
                                if (info.unique_id.empty())
                                    info.unique_id = "dshow:" + dshow_name;

                                fprintf(stderr, "[VideoCapture]   dshow[%d]: '%s'\n",
                                        info.index, info.name.c_str());
                                result.push_back(std::move(info));
                            }
                            VariantClear(&var);
                        }
                        pBag->Release();
                    }
                    pMoniker->Release();
                }
                pEnum->Release();
            }
            pDevEnum->Release();
        }
    }

    if (comInited) CoUninitialize();
    return result;
}

void CameraSource::request_permission()
{
    /* Windows 10 1803+: camera access is controlled by Settings → Privacy.
     * There's no runtime prompt API equivalent to macOS AVCaptureDevice.
     * If access is denied, MFEnumDeviceSources returns 0 devices.
     * Nothing to do here. */
}

void CameraSource::capture_loop()
{
    /* COM must be initialized on this thread for MF source reader */
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    auto *pReader = static_cast<IMFSourceReader*>(source_reader_);
    if (!pReader) {
        running_.store(false);
        CoUninitialize();
        return;
    }

    while (running_.load()) {
        DWORD    streamIndex = 0;
        DWORD    flags       = 0;
        LONGLONG timestamp   = 0;
        IMFSample *pSample   = nullptr;

        HRESULT hr = pReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,              /* dwControlFlags — 0 = blocking read */
            &streamIndex,
            &flags,
            &timestamp,
            &pSample);

        if (FAILED(hr)) {
            fprintf(stderr, "[VideoCapture] ReadSample failed: 0x%08lX\n", hr);
            break;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            fprintf(stderr, "[VideoCapture] End of stream\n");
            break;
        }

        if (flags & MF_SOURCE_READERF_STREAMTICK) {
            /* Gap in data — skip this sample */
            if (pSample) pSample->Release();
            continue;
        }

        if (!pSample) continue;

        /* Get the buffer from the sample */
        IMFMediaBuffer *pBuffer = nullptr;
        hr = pSample->ConvertToContiguousBuffer(&pBuffer);
        if (FAILED(hr) || !pBuffer) {
            pSample->Release();
            continue;
        }

        BYTE  *pData   = nullptr;
        DWORD  cbMax   = 0;
        DWORD  cbLen   = 0;
        hr = pBuffer->Lock(&pData, &cbMax, &cbLen);
        if (SUCCEEDED(hr) && pData && cbLen > 0) {
            const uint8_t *frame_data = pData;
            int frame_stride = stride_;
            size_t frame_len = static_cast<size_t>(cbLen);

            // SEC-017: Guard against zero or negative stride
            if (stride_ <= 0 || width_ <= 0 || height_ <= 0) {
                fprintf(stderr, "[VideoCapture] Invalid frame dimensions: stride=%d width=%d height=%d\n",
                        stride_, width_, height_);
                pBuffer->Unlock();
                pBuffer->Release();
                pSample->Release();
                continue;
            }

            /* ── Convert NV12/YUY2 → BGRA if needed ── */
            if (capture_fmt_ == 1) {
                /* NV12: Y plane (w*h) + interleaved UV plane (w*h/2) */
                const int rgb_stride = width_ * 4;
                rgb_buf_.resize(static_cast<size_t>(rgb_stride) * height_);
                const uint8_t *yp = pData;
                const uint8_t *uvp = pData + static_cast<size_t>(stride_) * height_;
                for (int row = 0; row < height_; ++row) {
                    for (int col = 0; col < width_; ++col) {
                        int y  = yp[row * stride_ + col];
                        int uv_row = row / 2;
                        int uv_col = (col / 2) * 2;
                        int u  = uvp[uv_row * stride_ + uv_col]     - 128;
                        int v  = uvp[uv_row * stride_ + uv_col + 1] - 128;
                        int r  = y + ((v * 359) >> 8);
                        int g  = y - ((u * 88 + v * 183) >> 8);
                        int b  = y + ((u * 454) >> 8);
                        auto clamp = [](int x) -> uint8_t {
                            return static_cast<uint8_t>(x < 0 ? 0 : (x > 255 ? 255 : x));
                        };
                        uint8_t *px = rgb_buf_.data() + row * rgb_stride + col * 4;
                        px[0] = clamp(b); px[1] = clamp(g);
                        px[2] = clamp(r); px[3] = 0xFF;
                    }
                }
                frame_data = rgb_buf_.data();
                frame_stride = rgb_stride;
                frame_len = rgb_buf_.size();
            } else if (capture_fmt_ == 2) {
                /* YUY2: packed YUYV, 2 bytes per pixel, 2 pixels share U/V */
                const int rgb_stride = width_ * 4;
                rgb_buf_.resize(static_cast<size_t>(rgb_stride) * height_);
                for (int row = 0; row < height_; ++row) {
                    const uint8_t *src_row = pData + row * stride_;
                    uint8_t *dst_row = rgb_buf_.data() + row * rgb_stride;
                    for (int col = 0; col < width_; col += 2) {
                        int y0 = src_row[col * 2];
                        int u  = src_row[col * 2 + 1] - 128;
                        int y1 = src_row[col * 2 + 2];
                        int v  = src_row[col * 2 + 3] - 128;
                        auto clamp = [](int x) -> uint8_t {
                            return static_cast<uint8_t>(x < 0 ? 0 : (x > 255 ? 255 : x));
                        };
                        /* Pixel 0 */
                        dst_row[col*4+0] = clamp(y0 + ((u * 454) >> 8));
                        dst_row[col*4+1] = clamp(y0 - ((u * 88 + v * 183) >> 8));
                        dst_row[col*4+2] = clamp(y0 + ((v * 359) >> 8));
                        dst_row[col*4+3] = 0xFF;
                        /* Pixel 1 */
                        dst_row[col*4+4] = clamp(y1 + ((u * 454) >> 8));
                        dst_row[col*4+5] = clamp(y1 - ((u * 88 + v * 183) >> 8));
                        dst_row[col*4+6] = clamp(y1 + ((v * 359) >> 8));
                        dst_row[col*4+7] = 0xFF;
                    }
                }
                frame_data = rgb_buf_.data();
                frame_stride = rgb_stride;
                frame_len = rgb_buf_.size();
            }
            /* else: capture_fmt_==0 → RGB32, no conversion needed */

            /* Bottom-up: flip rows so downstream sees top-down */
            if (bottom_up_ && height_ > 1) {
                flip_buf_.resize(static_cast<size_t>(frame_stride) * height_);
                for (int row = 0; row < height_; ++row) {
                    const uint8_t *src = frame_data + static_cast<size_t>(frame_stride) * (height_ - 1 - row);
                    uint8_t *dst = flip_buf_.data() + static_cast<size_t>(frame_stride) * row;
                    std::memcpy(dst, src, frame_stride);
                }
                frame_data = flip_buf_.data();
                frame_len  = flip_buf_.size();
            }

            /* Deliver frame via callback — always BGRA, positive stride */
            VideoFrame frame;
            frame.data         = frame_data;
            frame.data_len     = frame_len;
            frame.width        = width_;
            frame.height       = height_;
            frame.stride       = (capture_fmt_ == 0) ? frame_stride : width_ * 4;
            frame.pixel_format = 0;            /* BGRA */
            frame.pts_us       = timestamp / 10; /* MF uses 100-ns units → μs */

            if (callback_)
                callback_(frame);

            pBuffer->Unlock();
        }

        pBuffer->Release();
        pSample->Release();
    }

    CoUninitialize();
    running_.store(false);
}

} // namespace mc1
