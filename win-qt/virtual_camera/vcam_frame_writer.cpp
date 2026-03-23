/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * virtual_camera/vcam_frame_writer.cpp — Shared memory frame writer
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "vcam_frame_writer.h"
#include "vcam_shared_memory.h"

#include <cstdio>
#include <cstring>

namespace mc1 {

VCamFrameWriter::VCamFrameWriter() = default;

VCamFrameWriter::~VCamFrameWriter()
{
    close();
}

bool VCamFrameWriter::open(int width, int height, int fps, int slot_index)
{
    close();

    width_  = width;
    height_ = height;
    fps_    = fps;

    /* Build shared memory name: "Local\Mcaster1VCam_0" */
    wchar_t shm_name[64];
    swprintf_s(shm_name, 64, L"%s%d", kVCamShmPrefix, slot_index);

    size_t total_size = vcam_shm_size(width, height);

    /* Create the file mapping (shared memory) */
    shm_handle_ = CreateFileMappingW(
        INVALID_HANDLE_VALUE,   /* backed by page file */
        nullptr,                /* default security */
        PAGE_READWRITE,
        static_cast<DWORD>(total_size >> 32),
        static_cast<DWORD>(total_size & 0xFFFFFFFF),
        shm_name);

    if (!shm_handle_) return false;

    shm_ptr_ = MapViewOfFile(
        static_cast<HANDLE>(shm_handle_), FILE_MAP_ALL_ACCESS, 0, 0, total_size);
    if (!shm_ptr_) {
        CloseHandle(static_cast<HANDLE>(shm_handle_));
        shm_handle_ = nullptr;
        return false;
    }

    /* Initialize header */
    auto *hdr = static_cast<VCamSharedHeader *>(shm_ptr_);
    memset(hdr, 0, sizeof(VCamSharedHeader));
    hdr->magic  = static_cast<uint32_t>(kVCamMagic);
    hdr->width  = static_cast<uint32_t>(width);
    hdr->height = static_cast<uint32_t>(height);
    hdr->stride = static_cast<uint32_t>(width * 4);
    hdr->fps    = static_cast<uint32_t>(fps);
    hdr->active = 1;
    hdr->frame_counter = 0;
    frame_counter_ = 0;

    /* Create named event for frame signaling */
    wchar_t evt_name[64];
    swprintf_s(evt_name, 64, L"%s%d", kVCamEventPrefix, slot_index);
    event_handle_ = CreateEventW(nullptr, FALSE, FALSE, evt_name);
    /* auto-reset event: WaitForSingleObject resets it automatically */

    return true;
}

void VCamFrameWriter::close()
{
    if (shm_ptr_) {
        /* Mark inactive so the DLL delivers black frames */
        auto *hdr = static_cast<VCamSharedHeader *>(shm_ptr_);
        hdr->active = 0;

        UnmapViewOfFile(shm_ptr_);
        shm_ptr_ = nullptr;
    }
    if (shm_handle_) {
        CloseHandle(static_cast<HANDLE>(shm_handle_));
        shm_handle_ = nullptr;
    }
    if (event_handle_) {
        CloseHandle(static_cast<HANDLE>(event_handle_));
        event_handle_ = nullptr;
    }
}

void VCamFrameWriter::pushFrame(const uint8_t *bgra, int width, int height,
                                 int stride)
{
    if (!shm_ptr_) return;

    auto *hdr = static_cast<VCamSharedHeader *>(shm_ptr_);
    uint8_t *dst = reinterpret_cast<uint8_t *>(shm_ptr_) + sizeof(VCamSharedHeader);

    if (width <= 0 || height <= 0 || stride <= 0 || !bgra) return;
    if (width > 8192 || height > 8192 || stride > 32768) return;

    int copy_w = (width < width_)   ? width  : width_;
    int copy_h = (height < height_) ? height : height_;
    int dst_stride = width_ * 4;

    /* Validate shared memory bounds */
    size_t shm_data_size = static_cast<size_t>(dst_stride) * static_cast<size_t>(height_);
    size_t required = sizeof(VCamSharedHeader) + shm_data_size;
    (void)required; /* shm was allocated with this size at init */

    /* Copy row-by-row (handles stride mismatch) */
    int row_bytes = copy_w * 4;
    for (int y = 0; y < copy_h; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * dst_stride,
                    bgra + static_cast<size_t>(y) * stride, row_bytes);
    }

    /* Update header (frame_counter signals new data to DLL reader) */
    hdr->width  = static_cast<uint32_t>(width_);
    hdr->height = static_cast<uint32_t>(height_);
    hdr->stride = static_cast<uint32_t>(dst_stride);
    hdr->frame_counter = ++frame_counter_;

    /* Signal the event to wake up the DLL's delivery thread */
    if (event_handle_)
        SetEvent(static_cast<HANDLE>(event_handle_));
}

void VCamFrameWriter::setActive(bool active)
{
    if (!shm_ptr_) return;
    auto *hdr = static_cast<VCamSharedHeader *>(shm_ptr_);
    hdr->active = active ? 1 : 0;
}

} // namespace mc1
