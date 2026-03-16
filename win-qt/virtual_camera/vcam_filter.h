/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * virtual_camera/vcam_filter.h — DirectShow source filter for virtual camera
 *
 * Implements IBaseFilter with a single output pin that reads BGRA frames
 * from shared memory and delivers them as a standard video capture device.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VCAM_FILTER_H
#define MC1_VCAM_FILTER_H

#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>  /* VIDEOINFOHEADER2 */

namespace mc1 {

class VCamPin;

/* ═══════════════════════════════════════════════════════════════════════
 * VCamFilter — DirectShow source filter (IBaseFilter)
 * ═══════════════════════════════════════════════════════════════════════ */
class VCamFilter : public IBaseFilter {
public:
    VCamFilter();
    ~VCamFilter();

    /* IUnknown */
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    /* IPersist */
    STDMETHODIMP GetClassID(CLSID *pClassID) override;

    /* IMediaFilter */
    STDMETHODIMP Stop() override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Run(REFERENCE_TIME tStart) override;
    STDMETHODIMP GetState(DWORD dwMilliSecsTimeout, FILTER_STATE *pState) override;
    STDMETHODIMP SetSyncSource(IReferenceClock *pClock) override;
    STDMETHODIMP GetSyncSource(IReferenceClock **pClock) override;

    /* IBaseFilter */
    STDMETHODIMP EnumPins(IEnumPins **ppEnum) override;
    STDMETHODIMP FindPin(LPCWSTR Id, IPin **ppPin) override;
    STDMETHODIMP QueryFilterInfo(FILTER_INFO *pInfo) override;
    STDMETHODIMP JoinFilterGraph(IFilterGraph *pGraph, LPCWSTR pName) override;
    STDMETHODIMP QueryVendorInfo(LPWSTR *pVendorInfo) override;

    FILTER_STATE state() const { return state_; }
    IFilterGraph *graph() const { return graph_; }

private:
    LONG             ref_count_ = 1;
    FILTER_STATE     state_     = State_Stopped;
    IReferenceClock *clock_     = nullptr;
    IFilterGraph    *graph_     = nullptr;
    VCamPin         *pin_       = nullptr;
    wchar_t          name_[128] = {};
};

/* ═══════════════════════════════════════════════════════════════════════
 * VCamPin — Output pin (IPin + IAMStreamConfig + IKsPropertySet)
 * ═══════════════════════════════════════════════════════════════════════ */
class VCamPin : public IPin, public IAMStreamConfig, public IKsPropertySet {
public:
    explicit VCamPin(VCamFilter *filter);
    ~VCamPin();

    /* IUnknown */
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    /* IPin */
    STDMETHODIMP Connect(IPin *pReceivePin, const AM_MEDIA_TYPE *pmt) override;
    STDMETHODIMP ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt) override;
    STDMETHODIMP Disconnect() override;
    STDMETHODIMP ConnectedTo(IPin **ppPin) override;
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE *pmt) override;
    STDMETHODIMP QueryPinInfo(PIN_INFO *pInfo) override;
    STDMETHODIMP QueryDirection(PIN_DIRECTION *pPinDir) override;
    STDMETHODIMP QueryId(LPWSTR *Id) override;
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE *pmt) override;
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes **ppEnum) override;
    STDMETHODIMP QueryInternalConnections(IPin **apPin, ULONG *nPin) override;
    STDMETHODIMP EndOfStream() override;
    STDMETHODIMP BeginFlush() override;
    STDMETHODIMP EndFlush() override;
    STDMETHODIMP NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop,
                            double dRate) override;

    /* IAMStreamConfig */
    STDMETHODIMP SetFormat(AM_MEDIA_TYPE *pmt) override;
    STDMETHODIMP GetFormat(AM_MEDIA_TYPE **ppmt) override;
    STDMETHODIMP GetNumberOfCapabilities(int *piCount, int *piSize) override;
    STDMETHODIMP GetStreamCaps(int iIndex, AM_MEDIA_TYPE **ppmt,
                               BYTE *pSCC) override;

    /* IKsPropertySet — needed for PIN_CATEGORY identification */
    STDMETHODIMP Set(REFGUID guidPropSet, DWORD dwPropID,
                     LPVOID pInstanceData, DWORD cbInstanceData,
                     LPVOID pPropData, DWORD cbPropData) override;
    STDMETHODIMP Get(REFGUID guidPropSet, DWORD dwPropID,
                     LPVOID pInstanceData, DWORD cbInstanceData,
                     LPVOID pPropData, DWORD cbPropData,
                     DWORD *pcbReturned) override;
    STDMETHODIMP QuerySupported(REFGUID guidPropSet, DWORD dwPropID,
                                DWORD *pTypeSupport) override;

    /* Delivery control (called by VCamFilter) */
    void StartDelivery();
    void StopDelivery();

private:
    static DWORD WINAPI DeliverThreadProc(void *param);
    void DeliverLoop();

    void FillMediaType(AM_MEDIA_TYPE *pmt, int width, int height, int fps);
    bool OpenSharedMemory();
    void CloseSharedMemory();

    VCamFilter   *filter_         = nullptr;
    LONG          ref_count_      = 1;

    /* Connection state */
    IPin          *connected_pin_ = nullptr;
    IMemInputPin  *mem_input_     = nullptr;
    IMemAllocator *allocator_     = nullptr;
    AM_MEDIA_TYPE  media_type_    = {};

    /* Delivery thread */
    HANDLE         thread_        = nullptr;
    bool           delivering_    = false;

    /* Shared memory */
    HANDLE         shm_handle_   = nullptr;
    void          *shm_ptr_      = nullptr;
    HANDLE         event_handle_ = nullptr;

    /* Current format */
    int            width_  = 1280;
    int            height_ = 720;
    int            fps_    = 30;
};

/* ═══════════════════════════════════════════════════════════════════════
 * VCamEnumPins — IEnumPins implementation
 * ═══════════════════════════════════════════════════════════════════════ */
class VCamEnumPins : public IEnumPins {
public:
    VCamEnumPins(IPin *pin);

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Next(ULONG cPins, IPin **ppPins, ULONG *pcFetched) override;
    STDMETHODIMP Skip(ULONG cPins) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Clone(IEnumPins **ppEnum) override;

private:
    LONG   ref_count_ = 1;
    IPin  *pin_       = nullptr;
    int    pos_       = 0;
};

/* ═══════════════════════════════════════════════════════════════════════
 * VCamEnumMediaTypes — IEnumMediaTypes implementation
 * ═══════════════════════════════════════════════════════════════════════ */
class VCamEnumMediaTypes : public IEnumMediaTypes {
public:
    VCamEnumMediaTypes(VCamPin *pin);

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Next(ULONG cMediaTypes, AM_MEDIA_TYPE **ppMediaTypes,
                      ULONG *pcFetched) override;
    STDMETHODIMP Skip(ULONG cMediaTypes) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Clone(IEnumMediaTypes **ppEnum) override;

private:
    LONG      ref_count_ = 1;
    VCamPin  *pin_       = nullptr;
    int       pos_       = 0;
};

/* ═══════════════════════════════════════════════════════════════════════
 * VCamClassFactory — IClassFactory for COM object creation
 * ═══════════════════════════════════════════════════════════════════════ */
class VCamClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv) override;
    STDMETHODIMP LockServer(BOOL fLock) override;

private:
    LONG ref_count_ = 1;
};

/* Global lock count for DllCanUnloadNow */
extern LONG g_vcam_server_locks;
extern LONG g_vcam_object_count;

} // namespace mc1

#endif // MC1_VCAM_FILTER_H
