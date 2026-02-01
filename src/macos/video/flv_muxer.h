/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/flv_muxer.h — FLV container muxer for audio + video streaming
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_FLV_MUXER_H
#define MC1_FLV_MUXER_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

namespace mc1 {

class FlvMuxer {
public:
    using OutputCallback = std::function<void(const uint8_t* data, size_t len)>;

    FlvMuxer();

    void set_output(OutputCallback cb) { output_ = cb; }

    /* Write FLV file header (must call first) */
    void write_header(bool has_audio, bool has_video);

    /* Write AVC (H.264) sequence header (SPS/PPS) — call before first video frame */
    void write_avc_sequence_header(const uint8_t* sps, size_t sps_len,
                                    const uint8_t* pps, size_t pps_len,
                                    uint32_t timestamp_ms);

    /* Write H.264 video NALU (Annex B format) */
    void write_video(const uint8_t* nalu_data, size_t nalu_len,
                      uint32_t timestamp_ms, bool is_keyframe);

    /* Write AAC AudioSpecificConfig — call before first audio frame */
    void write_aac_sequence_header(const uint8_t* asc, size_t asc_len,
                                    uint32_t timestamp_ms);

    /* Write AAC audio frame (raw AAC without ADTS) */
    void write_aac_audio(const uint8_t* frame, size_t frame_len,
                          uint32_t timestamp_ms);

    /* Write MP3 audio frame */
    void write_mp3_audio(const uint8_t* frame, size_t frame_len,
                          uint32_t timestamp_ms, int sample_rate, bool stereo);

private:
    void write_tag(uint8_t tag_type, const uint8_t* data, size_t data_len,
                    uint32_t timestamp_ms);
    void emit_bytes(const uint8_t* data, size_t len);

    /* Big-endian helpers */
    static void put_be16(uint8_t* p, uint16_t v);
    static void put_be24(uint8_t* p, uint32_t v);
    static void put_be32(uint8_t* p, uint32_t v);

    OutputCallback output_;
    uint32_t prev_tag_size_ = 0;
};

} // namespace mc1

#endif // MC1_FLV_MUXER_H
