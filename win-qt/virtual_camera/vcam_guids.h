/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * virtual_camera/vcam_guids.h — CLSID definitions for the virtual camera
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VCAM_GUIDS_H
#define MC1_VCAM_GUIDS_H

#include <initguid.h>

/*
 * CLSID_Mcaster1VirtualCam
 * {F7D3A1B2-C4E5-4F6D-8A9B-0C1D2E3F4A5B}
 *
 * This is the COM class ID for the Mcaster1 Virtual Camera DirectShow filter.
 * It must be unique system-wide. Registered in:
 *   HKCR\CLSID\{F7D3A1B2-C4E5-4F6D-8A9B-0C1D2E3F4A5B}
 * and in the Video Input Device category:
 *   HKCR\CLSID\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\Instance\{F7D3...}
 */
// {F7D3A1B2-C4E5-4F6D-8A9B-0C1D2E3F4A5B}
DEFINE_GUID(CLSID_Mcaster1VirtualCam,
    0xf7d3a1b2, 0xc4e5, 0x4f6d,
    0x8a, 0x9b, 0x0c, 0x1d, 0x2e, 0x3f, 0x4a, 0x5b);

namespace mc1 {
/* Friendly name shown in device lists */
constexpr const wchar_t *kVCamFriendlyName = L"Mcaster1 Virtual Camera";

/* Filter vendor info */
constexpr const wchar_t *kVCamVendorInfo = L"Mcaster1DSPEncoder Virtual Camera";
} // namespace mc1

/* Video Input Device category (standard DirectShow GUID) */
// {860BB310-5D01-11d0-BD3B-00A0C911CE86}
DEFINE_GUID(CLSID_VideoInputDeviceCat,
    0x860bb310, 0x5d01, 0x11d0,
    0xbd, 0x3b, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86);

#endif // MC1_VCAM_GUIDS_H
