/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * virtual_camera/vcam_frame_writer.h — Writes program output to shared memory
 *
 * The main application creates a VCamFrameWriter and calls pushFrame()
 * with each composited program output frame. The DirectShow filter DLL
 * reads these frames from shared memory and delivers them to consumers.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VCAM_FRAME_WRITER_H
#define MC1_VCAM_FRAME_WRITER_H

#include <cstdint>

namespace mc1 {

class VCamFrameWriter {
public:
    VCamFrameWriter();
    ~VCamFrameWriter();

    /* Create the shared memory section and event.
     * Call once before pushFrame(). Returns true on success.
     * slot_index: 0 for the first virtual camera, 1 for second, etc. */
    bool open(int width, int height, int fps, int slot_index = 0);

    /* Close the shared memory section. */
    void close();

    /* Push a BGRA frame to shared memory. Thread-safe.
     * The DirectShow filter DLL will read this frame. */
    void pushFrame(const uint8_t *bgra, int width, int height, int stride);

    /* Mark as inactive (delivers black frames in the DLL). */
    void setActive(bool active);

    bool is_open() const { return shm_ptr_ != nullptr; }

private:
    void    *shm_handle_    = nullptr;  /* HANDLE — opaque to avoid windows.h in header */
    void    *shm_ptr_       = nullptr;
    void    *event_handle_  = nullptr;  /* HANDLE */
    uint64_t frame_counter_ = 0;
    int      width_         = 0;
    int      height_        = 0;
    int      fps_           = 30;
};

} // namespace mc1

#endif // MC1_VCAM_FRAME_WRITER_H
