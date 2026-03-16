/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * virtual_camera/vcam_shared_memory.h — Shared memory layout for virtual camera
 *
 * This header is used by BOTH the main application (frame writer)
 * and the DirectShow filter DLL (frame reader). Keep it self-contained
 * with no Qt or project-specific dependencies.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VCAM_SHARED_MEMORY_H
#define MC1_VCAM_SHARED_MEMORY_H

#include <cstdint>
#include <cstddef>

namespace mc1 {

/* Magic value: "MC1V" in little-endian */
constexpr uint32_t kVCamMagic = 0x5631434D;

/* Maximum supported resolution */
constexpr int kVCamMaxWidth  = 1920;
constexpr int kVCamMaxHeight = 1080;

/* Shared memory name: L"Local\\Mcaster1VCam_0" etc. */
constexpr const wchar_t *kVCamShmPrefix   = L"Local\\Mcaster1VCam_";

/* Named event: L"Local\\Mcaster1VCamEvt_0" etc. (signaled on new frame) */
constexpr const wchar_t *kVCamEventPrefix = L"Local\\Mcaster1VCamEvt_";

/*
 * Shared memory layout:
 *   [VCamSharedHeader]  (64 bytes, cache-line aligned)
 *   [BGRA pixel data]   (stride * height bytes)
 */
#pragma pack(push, 1)
struct VCamSharedHeader {
    uint32_t magic;           /* kVCamMagic — validates mapping */
    uint32_t width;           /* frame width in pixels */
    uint32_t height;          /* frame height in pixels */
    uint32_t stride;          /* bytes per row (width * 4 for BGRA) */
    uint32_t fps;             /* target frame rate */
    uint32_t active;          /* 1 = writer is producing frames */
    uint64_t frame_counter;   /* monotonically increasing, writer increments */
    uint64_t timestamp_100ns; /* presentation timestamp in 100ns units (MF REFERENCE_TIME) */
    uint32_t reserved[6];     /* pad to 64 bytes */
};
#pragma pack(pop)

static_assert(sizeof(VCamSharedHeader) == 64, "VCamSharedHeader must be 64 bytes");

/* Total shared memory size for a given resolution */
inline size_t vcam_shm_size(int width, int height)
{
    return sizeof(VCamSharedHeader) +
           static_cast<size_t>(width) * 4 * static_cast<size_t>(height);
}

/* Max shared memory size (for pre-allocation at max resolution) */
inline size_t vcam_shm_max_size()
{
    return vcam_shm_size(kVCamMaxWidth, kVCamMaxHeight);
}

} // namespace mc1

#endif // MC1_VCAM_SHARED_MEMORY_H
