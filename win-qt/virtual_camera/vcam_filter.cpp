/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * virtual_camera/vcam_filter.cpp — DirectShow virtual camera filter
 *
 * Complete implementation of IBaseFilter + IPin + IAMStreamConfig
 * for a push-source virtual camera that reads from shared memory.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vcam_filter.h"
#include "vcam_guids.h"
#include "vcam_shared_memory.h"

#include <cstring>
#include <cstdio>

/* IKsPropertySet / AMPROPERTY_PIN GUIDs */
#include <ks.h>
#include <ksmedia.h>

#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "winmm.lib")

namespace mc1 {

LONG g_vcam_server_locks = 0;
LONG g_vcam_object_count = 0;

/* ═══════════════════════════════════════════════════════════════════════
 * Helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static void FreeMediaType(AM_MEDIA_TYPE &mt)
{
    if (mt.cbFormat > 0 && mt.pbFormat) {
        CoTaskMemFree(mt.pbFormat);
        mt.pbFormat = nullptr;
        mt.cbFormat = 0;
    }
    if (mt.pUnk) {
        mt.pUnk->Release();
        mt.pUnk = nullptr;
    }
}

static HRESULT CopyMediaType(AM_MEDIA_TYPE *dst, const AM_MEDIA_TYPE *src)
{
    *dst = *src;
    if (src->cbFormat > 0 && src->pbFormat) {
        dst->pbFormat = (BYTE *)CoTaskMemAlloc(src->cbFormat);
        if (!dst->pbFormat) return E_OUTOFMEMORY;
        memcpy(dst->pbFormat, src->pbFormat, src->cbFormat);
    }
    if (dst->pUnk) dst->pUnk->AddRef();
    return S_OK;
}

static AM_MEDIA_TYPE *AllocMediaType(const AM_MEDIA_TYPE *src)
{
    auto *pmt = (AM_MEDIA_TYPE *)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
    if (!pmt) return nullptr;
    if (FAILED(CopyMediaType(pmt, src))) {
        CoTaskMemFree(pmt);
        return nullptr;
    }
    return pmt;
}

/* ═══════════════════════════════════════════════════════════════════════
 * VCamFilter
 * ═══════════════════════════════════════════════════════════════════════ */

VCamFilter::VCamFilter()
{
    InterlockedIncrement(&g_vcam_object_count);
    pin_ = new VCamPin(this);
    wcscpy_s(name_, kVCamFriendlyName);
}

VCamFilter::~VCamFilter()
{
    if (pin_) { pin_->Release(); pin_ = nullptr; }
    if (clock_) { clock_->Release(); clock_ = nullptr; }
    InterlockedDecrement(&g_vcam_object_count);
}

/* IUnknown */

STDMETHODIMP VCamFilter::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown)       *ppv = static_cast<IUnknown *>(
                                        static_cast<IBaseFilter *>(this));
    else if (riid == IID_IPersist)       *ppv = static_cast<IPersist *>(this);
    else if (riid == IID_IMediaFilter)   *ppv = static_cast<IMediaFilter *>(this);
    else if (riid == IID_IBaseFilter)    *ppv = static_cast<IBaseFilter *>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) VCamFilter::AddRef()  { return InterlockedIncrement(&ref_count_); }
STDMETHODIMP_(ULONG) VCamFilter::Release()
{
    ULONG n = InterlockedDecrement(&ref_count_);
    if (n == 0) delete this;
    return n;
}

/* IPersist */
STDMETHODIMP VCamFilter::GetClassID(CLSID *pClassID)
{
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_Mcaster1VirtualCam;
    return S_OK;
}

/* IMediaFilter */

STDMETHODIMP VCamFilter::Stop()
{
    if (pin_) pin_->StopDelivery();
    state_ = State_Stopped;
    return S_OK;
}

STDMETHODIMP VCamFilter::Pause()
{
    if (state_ == State_Stopped && pin_)
        pin_->StartDelivery();
    state_ = State_Paused;
    return S_OK;
}

STDMETHODIMP VCamFilter::Run(REFERENCE_TIME /*tStart*/)
{
    if (state_ == State_Stopped && pin_)
        pin_->StartDelivery();
    state_ = State_Running;
    return S_OK;
}

STDMETHODIMP VCamFilter::GetState(DWORD /*dwMilliSecsTimeout*/,
                                   FILTER_STATE *pState)
{
    if (!pState) return E_POINTER;
    *pState = state_;
    return S_OK;
}

STDMETHODIMP VCamFilter::SetSyncSource(IReferenceClock *pClock)
{
    if (clock_) clock_->Release();
    clock_ = pClock;
    if (clock_) clock_->AddRef();
    return S_OK;
}

STDMETHODIMP VCamFilter::GetSyncSource(IReferenceClock **pClock)
{
    if (!pClock) return E_POINTER;
    *pClock = clock_;
    if (clock_) clock_->AddRef();
    return S_OK;
}

/* IBaseFilter */

STDMETHODIMP VCamFilter::EnumPins(IEnumPins **ppEnum)
{
    if (!ppEnum) return E_POINTER;
    *ppEnum = new VCamEnumPins(pin_);
    return S_OK;
}

STDMETHODIMP VCamFilter::FindPin(LPCWSTR Id, IPin **ppPin)
{
    if (!ppPin) return E_POINTER;
    if (wcscmp(Id, L"Output") == 0) {
        *ppPin = pin_;
        pin_->AddRef();
        return S_OK;
    }
    *ppPin = nullptr;
    return VFW_E_NOT_FOUND;
}

STDMETHODIMP VCamFilter::QueryFilterInfo(FILTER_INFO *pInfo)
{
    if (!pInfo) return E_POINTER;
    wcscpy_s(pInfo->achName, name_);
    pInfo->pGraph = graph_;
    if (graph_) graph_->AddRef();
    return S_OK;
}

STDMETHODIMP VCamFilter::JoinFilterGraph(IFilterGraph *pGraph, LPCWSTR pName)
{
    graph_ = pGraph; /* weak reference — do NOT AddRef */
    if (pName) wcscpy_s(name_, pName);
    return S_OK;
}

STDMETHODIMP VCamFilter::QueryVendorInfo(LPWSTR *pVendorInfo)
{
    if (!pVendorInfo) return E_POINTER;
    size_t len = wcslen(kVCamVendorInfo) + 1;
    *pVendorInfo = (LPWSTR)CoTaskMemAlloc(len * sizeof(wchar_t));
    if (!*pVendorInfo) return E_OUTOFMEMORY;
    wcscpy_s(*pVendorInfo, len, kVCamVendorInfo);
    return S_OK;
}

/* ═══════════════════════════════════════════════════════════════════════
 * VCamPin
 * ═══════════════════════════════════════════════════════════════════════ */

VCamPin::VCamPin(VCamFilter *filter) : filter_(filter)
{
    memset(&media_type_, 0, sizeof(media_type_));
    FillMediaType(&media_type_, width_, height_, fps_);
}

VCamPin::~VCamPin()
{
    StopDelivery();
    CloseSharedMemory();
    FreeMediaType(media_type_);
    if (connected_pin_) { connected_pin_->Release(); connected_pin_ = nullptr; }
    if (mem_input_) { mem_input_->Release(); mem_input_ = nullptr; }
    if (allocator_) { allocator_->Release(); allocator_ = nullptr; }
}

/* IUnknown */

STDMETHODIMP VCamPin::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown)          *ppv = static_cast<IUnknown *>(
                                           static_cast<IPin *>(this));
    else if (riid == IID_IPin)              *ppv = static_cast<IPin *>(this);
    else if (riid == IID_IAMStreamConfig)   *ppv = static_cast<IAMStreamConfig *>(this);
    else if (riid == IID_IKsPropertySet)    *ppv = static_cast<IKsPropertySet *>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) VCamPin::AddRef()  { return InterlockedIncrement(&ref_count_); }
STDMETHODIMP_(ULONG) VCamPin::Release()
{
    ULONG n = InterlockedDecrement(&ref_count_);
    if (n == 0) delete this;
    return n;
}

/* ── Media type helpers ─────────────────────────────────────────────── */

void VCamPin::FillMediaType(AM_MEDIA_TYPE *pmt, int width, int height, int fps)
{
    memset(pmt, 0, sizeof(*pmt));

    pmt->majortype            = MEDIATYPE_Video;
    pmt->subtype              = MEDIASUBTYPE_RGB32;
    pmt->bFixedSizeSamples    = TRUE;
    pmt->bTemporalCompression = FALSE;
    pmt->formattype           = FORMAT_VideoInfo;

    int stride = width * 4;
    pmt->lSampleSize = stride * height;
    pmt->cbFormat    = sizeof(VIDEOINFOHEADER);
    pmt->pbFormat    = (BYTE *)CoTaskMemAlloc(sizeof(VIDEOINFOHEADER));

    auto *vih = reinterpret_cast<VIDEOINFOHEADER *>(pmt->pbFormat);
    memset(vih, 0, sizeof(*vih));

    vih->bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
    vih->bmiHeader.biWidth         = width;
    vih->bmiHeader.biHeight        = height; /* positive = bottom-up */
    vih->bmiHeader.biPlanes        = 1;
    vih->bmiHeader.biBitCount      = 32;
    vih->bmiHeader.biCompression   = BI_RGB;
    vih->bmiHeader.biSizeImage     = stride * height;

    /* Frame duration in 100ns units: 10000000 / fps */
    vih->AvgTimePerFrame = 10000000LL / fps;
}

/* ── IPin ────────────────────────────────────────────────────────────── */

STDMETHODIMP VCamPin::Connect(IPin *pReceivePin, const AM_MEDIA_TYPE *pmt)
{
    if (!pReceivePin) return E_POINTER;
    if (connected_pin_) return VFW_E_ALREADY_CONNECTED;

    AM_MEDIA_TYPE proposed;
    if (pmt) {
        CopyMediaType(&proposed, pmt);
    } else {
        CopyMediaType(&proposed, &media_type_);
    }

    /* Try to connect with our proposed type */
    HRESULT hr = pReceivePin->ReceiveConnection(this, &proposed);
    if (FAILED(hr)) {
        FreeMediaType(proposed);
        return hr;
    }

    /* Get the downstream pin's IMemInputPin for sample delivery */
    IMemInputPin *memInput = nullptr;
    hr = pReceivePin->QueryInterface(IID_IMemInputPin, (void **)&memInput);
    if (FAILED(hr)) {
        pReceivePin->Disconnect();
        FreeMediaType(proposed);
        return hr;
    }

    /* Set up allocator */
    IMemAllocator *alloc = nullptr;
    hr = memInput->GetAllocator(&alloc);
    if (FAILED(hr)) {
        hr = CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IMemAllocator, (void **)&alloc);
    }
    if (FAILED(hr) || !alloc) {
        memInput->Release();
        pReceivePin->Disconnect();
        FreeMediaType(proposed);
        return hr;
    }

    ALLOCATOR_PROPERTIES request = {}, actual = {};
    request.cBuffers = 2;
    request.cbBuffer = proposed.lSampleSize;
    request.cbAlign  = 1;
    hr = alloc->SetProperties(&request, &actual);
    if (FAILED(hr)) {
        alloc->Release();
        memInput->Release();
        pReceivePin->Disconnect();
        FreeMediaType(proposed);
        return hr;
    }

    hr = memInput->NotifyAllocator(alloc, FALSE);
    if (FAILED(hr)) {
        alloc->Release();
        memInput->Release();
        pReceivePin->Disconnect();
        FreeMediaType(proposed);
        return hr;
    }

    /* Store connection state */
    connected_pin_ = pReceivePin;
    connected_pin_->AddRef();
    mem_input_ = memInput;
    allocator_ = alloc;
    FreeMediaType(media_type_);
    CopyMediaType(&media_type_, &proposed);
    FreeMediaType(proposed);

    return S_OK;
}

STDMETHODIMP VCamPin::ReceiveConnection(IPin *, const AM_MEDIA_TYPE *)
{
    return E_UNEXPECTED; /* output pin — cannot receive */
}

STDMETHODIMP VCamPin::Disconnect()
{
    StopDelivery();
    if (allocator_) { allocator_->Decommit(); allocator_->Release(); allocator_ = nullptr; }
    if (mem_input_) { mem_input_->Release(); mem_input_ = nullptr; }
    if (connected_pin_) { connected_pin_->Release(); connected_pin_ = nullptr; }
    return S_OK;
}

STDMETHODIMP VCamPin::ConnectedTo(IPin **ppPin)
{
    if (!ppPin) return E_POINTER;
    if (!connected_pin_) { *ppPin = nullptr; return VFW_E_NOT_CONNECTED; }
    *ppPin = connected_pin_;
    connected_pin_->AddRef();
    return S_OK;
}

STDMETHODIMP VCamPin::ConnectionMediaType(AM_MEDIA_TYPE *pmt)
{
    if (!pmt) return E_POINTER;
    if (!connected_pin_) return VFW_E_NOT_CONNECTED;
    return CopyMediaType(pmt, &media_type_);
}

STDMETHODIMP VCamPin::QueryPinInfo(PIN_INFO *pInfo)
{
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = filter_;
    if (filter_) filter_->AddRef();
    pInfo->dir = PINDIR_OUTPUT;
    wcscpy_s(pInfo->achName, L"Output");
    return S_OK;
}

STDMETHODIMP VCamPin::QueryDirection(PIN_DIRECTION *pPinDir)
{
    if (!pPinDir) return E_POINTER;
    *pPinDir = PINDIR_OUTPUT;
    return S_OK;
}

STDMETHODIMP VCamPin::QueryId(LPWSTR *Id)
{
    if (!Id) return E_POINTER;
    *Id = (LPWSTR)CoTaskMemAlloc(16 * sizeof(wchar_t));
    if (!*Id) return E_OUTOFMEMORY;
    wcscpy_s(*Id, 16, L"Output");
    return S_OK;
}

STDMETHODIMP VCamPin::QueryAccept(const AM_MEDIA_TYPE *pmt)
{
    if (!pmt) return E_POINTER;
    if (pmt->majortype != MEDIATYPE_Video) return S_FALSE;
    if (pmt->subtype != MEDIASUBTYPE_RGB32) return S_FALSE;
    return S_OK;
}

STDMETHODIMP VCamPin::EnumMediaTypes(IEnumMediaTypes **ppEnum)
{
    if (!ppEnum) return E_POINTER;
    *ppEnum = new VCamEnumMediaTypes(this);
    return S_OK;
}

STDMETHODIMP VCamPin::QueryInternalConnections(IPin **, ULONG *nPin)
{
    if (nPin) *nPin = 0;
    return E_NOTIMPL;
}

STDMETHODIMP VCamPin::EndOfStream()      { return S_OK; }
STDMETHODIMP VCamPin::BeginFlush()       { return S_OK; }
STDMETHODIMP VCamPin::EndFlush()         { return S_OK; }
STDMETHODIMP VCamPin::NewSegment(REFERENCE_TIME, REFERENCE_TIME, double)
{
    return S_OK;
}

/* ── IAMStreamConfig ──────────────────────────────────────────────────── */

STDMETHODIMP VCamPin::SetFormat(AM_MEDIA_TYPE *pmt)
{
    if (!pmt) return E_POINTER;
    if (pmt->majortype != MEDIATYPE_Video) return VFW_E_INVALIDMEDIATYPE;
    if (pmt->subtype != MEDIASUBTYPE_RGB32) return VFW_E_INVALIDMEDIATYPE;

    auto *vih = reinterpret_cast<VIDEOINFOHEADER *>(pmt->pbFormat);
    if (!vih) return VFW_E_INVALIDMEDIATYPE;

    width_  = vih->bmiHeader.biWidth;
    height_ = abs(vih->bmiHeader.biHeight);
    if (vih->AvgTimePerFrame > 0)
        fps_ = static_cast<int>(10000000LL / vih->AvgTimePerFrame);

    FreeMediaType(media_type_);
    FillMediaType(&media_type_, width_, height_, fps_);
    return S_OK;
}

STDMETHODIMP VCamPin::GetFormat(AM_MEDIA_TYPE **ppmt)
{
    if (!ppmt) return E_POINTER;
    *ppmt = AllocMediaType(&media_type_);
    return *ppmt ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP VCamPin::GetNumberOfCapabilities(int *piCount, int *piSize)
{
    if (!piCount || !piSize) return E_POINTER;
    /* Offer 3 resolutions: 480p, 720p, 1080p */
    *piCount = 3;
    *piSize  = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

STDMETHODIMP VCamPin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE **ppmt,
                                     BYTE *pSCC)
{
    if (!ppmt || !pSCC) return E_POINTER;

    struct { int w, h, fps; } caps[] = {
        { 854,  480, 30 },
        { 1280, 720, 30 },
        { 1920, 1080, 30 },
    };
    if (iIndex < 0 || iIndex >= 3) return S_FALSE;

    AM_MEDIA_TYPE mt;
    FillMediaType(&mt, caps[iIndex].w, caps[iIndex].h, caps[iIndex].fps);
    *ppmt = AllocMediaType(&mt);
    FreeMediaType(mt);

    auto *scc = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS *>(pSCC);
    memset(scc, 0, sizeof(*scc));
    scc->guid               = FORMAT_VideoInfo;
    scc->VideoStandard       = 0;
    scc->InputSize.cx        = caps[iIndex].w;
    scc->InputSize.cy        = caps[iIndex].h;
    scc->MinCroppingSize.cx  = caps[iIndex].w;
    scc->MinCroppingSize.cy  = caps[iIndex].h;
    scc->MaxCroppingSize.cx  = caps[iIndex].w;
    scc->MaxCroppingSize.cy  = caps[iIndex].h;
    scc->CropGranularityX    = 1;
    scc->CropGranularityY    = 1;
    scc->CropAlignX          = 1;
    scc->CropAlignY          = 1;
    scc->MinOutputSize.cx    = caps[iIndex].w;
    scc->MinOutputSize.cy    = caps[iIndex].h;
    scc->MaxOutputSize.cx    = caps[iIndex].w;
    scc->MaxOutputSize.cy    = caps[iIndex].h;
    scc->OutputGranularityX  = 1;
    scc->OutputGranularityY  = 1;
    scc->MinFrameInterval    = 10000000LL / caps[iIndex].fps;
    scc->MaxFrameInterval    = 10000000LL / caps[iIndex].fps;
    scc->MinBitsPerSecond    = caps[iIndex].w * caps[iIndex].h * 32LL *
                               caps[iIndex].fps;
    scc->MaxBitsPerSecond    = scc->MinBitsPerSecond;

    return S_OK;
}

/* ── IKsPropertySet ──────────────────────────────────────────────────── */

STDMETHODIMP VCamPin::Set(REFGUID, DWORD, LPVOID, DWORD, LPVOID, DWORD)
{
    return E_NOTIMPL;
}

STDMETHODIMP VCamPin::Get(REFGUID guidPropSet, DWORD dwPropID,
                           LPVOID /*pInstanceData*/, DWORD /*cbInstanceData*/,
                           LPVOID pPropData, DWORD cbPropData,
                           DWORD *pcbReturned)
{
    if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
    if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
    if (cbPropData < sizeof(GUID)) return E_UNEXPECTED;

    /* Report as a CAPTURE pin (not preview) */
    *reinterpret_cast<GUID *>(pPropData) = PIN_CATEGORY_CAPTURE;
    if (pcbReturned) *pcbReturned = sizeof(GUID);
    return S_OK;
}

STDMETHODIMP VCamPin::QuerySupported(REFGUID guidPropSet, DWORD dwPropID,
                                      DWORD *pTypeSupport)
{
    if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
    if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
    if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
    return S_OK;
}

/* ── Shared Memory ───────────────────────────────────────────────────── */

bool VCamPin::OpenSharedMemory()
{
    if (shm_ptr_) return true; /* already open */

    /* Open shared memory section created by the main application */
    wchar_t shm_name[64];
    swprintf_s(shm_name, L"%s0", kVCamShmPrefix);

    shm_handle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, shm_name);
    if (!shm_handle_) return false;

    shm_ptr_ = MapViewOfFile(shm_handle_, FILE_MAP_READ, 0, 0, 0);
    if (!shm_ptr_) {
        CloseHandle(shm_handle_);
        shm_handle_ = nullptr;
        return false;
    }

    /* Open the event for frame signaling */
    wchar_t evt_name[64];
    swprintf_s(evt_name, L"%s0", kVCamEventPrefix);
    event_handle_ = OpenEventW(SYNCHRONIZE, FALSE, evt_name);
    /* event_handle_ can be null — we'll poll in that case */

    return true;
}

void VCamPin::CloseSharedMemory()
{
    if (event_handle_) { CloseHandle(event_handle_); event_handle_ = nullptr; }
    if (shm_ptr_) { UnmapViewOfFile(shm_ptr_); shm_ptr_ = nullptr; }
    if (shm_handle_) { CloseHandle(shm_handle_); shm_handle_ = nullptr; }
}

/* ── Frame Delivery Thread ───────────────────────────────────────────── */

void VCamPin::StartDelivery()
{
    if (delivering_) return;
    if (!connected_pin_ || !allocator_) return;

    allocator_->Commit();
    delivering_ = true;
    thread_ = CreateThread(nullptr, 0, DeliverThreadProc, this, 0, nullptr);
}

void VCamPin::StopDelivery()
{
    delivering_ = false;
    /* Signal the event to unblock the thread's wait */
    if (event_handle_) SetEvent(event_handle_);
    if (thread_) {
        WaitForSingleObject(thread_, 3000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    CloseSharedMemory();
}

DWORD WINAPI VCamPin::DeliverThreadProc(void *param)
{
    auto *pin = static_cast<VCamPin *>(param);
    pin->DeliverLoop();
    return 0;
}

void VCamPin::DeliverLoop()
{
    OpenSharedMemory();

    uint64_t last_frame_counter = 0;
    REFERENCE_TIME frame_duration = 10000000LL / fps_;
    REFERENCE_TIME sample_time = 0;

    while (delivering_) {
        /* Wait for new frame or timeout (delivers black frames if no source) */
        DWORD wait_ms = static_cast<DWORD>(frame_duration / 10000);
        if (event_handle_)
            WaitForSingleObject(event_handle_, wait_ms);
        else
            Sleep(wait_ms);

        if (!delivering_) break;

        /* Get an IMediaSample from the allocator */
        IMediaSample *pSample = nullptr;
        HRESULT hr = allocator_->GetBuffer(&pSample, nullptr, nullptr, 0);
        if (FAILED(hr) || !pSample) continue;

        BYTE *pData = nullptr;
        hr = pSample->GetPointer(&pData);
        if (FAILED(hr) || !pData) { pSample->Release(); continue; }

        long buf_size = pSample->GetSize();
        long frame_bytes = width_ * 4 * height_;

        /* Read from shared memory or generate black frame */
        bool got_frame = false;
        if (shm_ptr_) {
            auto *hdr = static_cast<const VCamSharedHeader *>(shm_ptr_);
            if (hdr->magic == kVCamMagic && hdr->active &&
                hdr->frame_counter != last_frame_counter) {
                /* Copy frame data from shared memory */
                const uint8_t *src = reinterpret_cast<const uint8_t *>(shm_ptr_)
                                     + sizeof(VCamSharedHeader);
                int src_stride = static_cast<int>(hdr->stride);
                int src_w = static_cast<int>(hdr->width);
                int src_h = static_cast<int>(hdr->height);

                /* Match dimensions — copy or scale */
                if (src_w == width_ && src_h == height_ && frame_bytes <= buf_size) {
                    memcpy(pData, src, frame_bytes);
                    got_frame = true;
                } else if (frame_bytes <= buf_size) {
                    /* Dimension mismatch — simple nearest-neighbor scale */
                    for (int y = 0; y < height_; ++y) {
                        int sy = y * src_h / height_;
                        const uint8_t *srow = src + sy * src_stride;
                        uint32_t *drow = reinterpret_cast<uint32_t *>(
                            pData + y * width_ * 4);
                        for (int x = 0; x < width_; ++x) {
                            int sx = x * src_w / width_;
                            drow[x] = reinterpret_cast<const uint32_t *>(srow)[sx];
                        }
                    }
                    got_frame = true;
                }
                last_frame_counter = hdr->frame_counter;
            }
        } else {
            /* Try to reopen shared memory (app may have started after us) */
            OpenSharedMemory();
        }

        if (!got_frame) {
            /* Deliver black frame */
            memset(pData, 0, (buf_size < frame_bytes) ? buf_size : frame_bytes);
        }

        pSample->SetActualDataLength(frame_bytes);

        /* Set presentation timestamps */
        REFERENCE_TIME start = sample_time;
        REFERENCE_TIME end   = sample_time + frame_duration;
        pSample->SetTime(&start, &end);
        pSample->SetSyncPoint(TRUE);
        sample_time = end;

        /* Deliver to downstream pin */
        hr = mem_input_->Receive(pSample);
        pSample->Release();

        if (FAILED(hr)) {
            /* Downstream rejected — stop delivery */
            break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * VCamEnumPins
 * ═══════════════════════════════════════════════════════════════════════ */

VCamEnumPins::VCamEnumPins(IPin *pin) : pin_(pin)
{
    if (pin_) pin_->AddRef();
}

STDMETHODIMP VCamEnumPins::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown)     *ppv = static_cast<IUnknown *>(this);
    else if (riid == IID_IEnumPins) *ppv = static_cast<IEnumPins *>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) VCamEnumPins::AddRef()  { return InterlockedIncrement(&ref_count_); }
STDMETHODIMP_(ULONG) VCamEnumPins::Release()
{
    ULONG n = InterlockedDecrement(&ref_count_);
    if (n == 0) {
        if (pin_) pin_->Release();
        delete this;
    }
    return n;
}

STDMETHODIMP VCamEnumPins::Next(ULONG cPins, IPin **ppPins, ULONG *pcFetched)
{
    ULONG fetched = 0;
    while (fetched < cPins && pos_ == 0) {
        ppPins[fetched] = pin_;
        pin_->AddRef();
        ++fetched;
        ++pos_;
    }
    if (pcFetched) *pcFetched = fetched;
    return (fetched == cPins) ? S_OK : S_FALSE;
}

STDMETHODIMP VCamEnumPins::Skip(ULONG cPins)
{
    pos_ += static_cast<int>(cPins);
    return (pos_ <= 1) ? S_OK : S_FALSE;
}

STDMETHODIMP VCamEnumPins::Reset() { pos_ = 0; return S_OK; }

STDMETHODIMP VCamEnumPins::Clone(IEnumPins **ppEnum)
{
    if (!ppEnum) return E_POINTER;
    auto *c = new VCamEnumPins(pin_);
    c->pos_ = pos_;
    *ppEnum = c;
    return S_OK;
}

/* ═══════════════════════════════════════════════════════════════════════
 * VCamEnumMediaTypes
 * ═══════════════════════════════════════════════════════════════════════ */

VCamEnumMediaTypes::VCamEnumMediaTypes(VCamPin *pin) : pin_(pin)
{
    if (pin_) pin_->AddRef();
}

STDMETHODIMP VCamEnumMediaTypes::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown)           *ppv = static_cast<IUnknown *>(this);
    else if (riid == IID_IEnumMediaTypes) *ppv = static_cast<IEnumMediaTypes *>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) VCamEnumMediaTypes::AddRef()
{ return InterlockedIncrement(&ref_count_); }

STDMETHODIMP_(ULONG) VCamEnumMediaTypes::Release()
{
    ULONG n = InterlockedDecrement(&ref_count_);
    if (n == 0) {
        if (pin_) pin_->Release();
        delete this;
    }
    return n;
}

STDMETHODIMP VCamEnumMediaTypes::Next(ULONG cMediaTypes,
                                       AM_MEDIA_TYPE **ppMediaTypes,
                                       ULONG *pcFetched)
{
    ULONG fetched = 0;
    while (fetched < cMediaTypes && pos_ == 0) {
        AM_MEDIA_TYPE *pmt = nullptr;
        if (SUCCEEDED(pin_->GetFormat(&pmt))) {
            ppMediaTypes[fetched] = pmt;
            ++fetched;
        }
        ++pos_;
    }
    if (pcFetched) *pcFetched = fetched;
    return (fetched == cMediaTypes) ? S_OK : S_FALSE;
}

STDMETHODIMP VCamEnumMediaTypes::Skip(ULONG cMediaTypes)
{
    pos_ += static_cast<int>(cMediaTypes);
    return (pos_ <= 1) ? S_OK : S_FALSE;
}

STDMETHODIMP VCamEnumMediaTypes::Reset() { pos_ = 0; return S_OK; }

STDMETHODIMP VCamEnumMediaTypes::Clone(IEnumMediaTypes **ppEnum)
{
    if (!ppEnum) return E_POINTER;
    auto *c = new VCamEnumMediaTypes(pin_);
    c->pos_ = pos_;
    *ppEnum = c;
    return S_OK;
}

/* ═══════════════════════════════════════════════════════════════════════
 * VCamClassFactory
 * ═══════════════════════════════════════════════════════════════════════ */

STDMETHODIMP VCamClassFactory::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown)         *ppv = static_cast<IUnknown *>(this);
    else if (riid == IID_IClassFactory) *ppv = static_cast<IClassFactory *>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) VCamClassFactory::AddRef()
{ return InterlockedIncrement(&ref_count_); }

STDMETHODIMP_(ULONG) VCamClassFactory::Release()
{
    ULONG n = InterlockedDecrement(&ref_count_);
    if (n == 0) delete this;
    return n;
}

STDMETHODIMP VCamClassFactory::CreateInstance(IUnknown *pUnkOuter, REFIID riid,
                                               void **ppv)
{
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    if (!ppv) return E_POINTER;

    auto *filter = new VCamFilter();
    HRESULT hr = filter->QueryInterface(riid, ppv);
    filter->Release(); /* QI AddRef'd if successful */
    return hr;
}

STDMETHODIMP VCamClassFactory::LockServer(BOOL fLock)
{
    if (fLock) InterlockedIncrement(&g_vcam_server_locks);
    else       InterlockedDecrement(&g_vcam_server_locks);
    return S_OK;
}

} // namespace mc1
