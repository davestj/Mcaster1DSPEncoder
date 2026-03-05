/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/webm_muxer.cpp — WebM (EBML/Matroska) container muxer
 *
 * EBML element IDs and semantics from the Matroska specification:
 *   https://www.matroska.org/technical/elements.html
 *
 * Key element IDs used:
 *   0x1A45DFA3 = EBML (master)
 *   0x4286     = EBMLVersion
 *   0x42F7     = EBMLReadVersion
 *   0x42F2     = EBMLMaxIDLength
 *   0x42F3     = EBMLMaxSizeLength
 *   0x4282     = DocType
 *   0x4287     = DocTypeVersion
 *   0x4285     = DocTypeReadVersion
 *   0x18538067 = Segment (master, unknown size for live)
 *   0x1549A966 = Info (master)
 *   0x2AD7B1   = TimestampScale (nanoseconds per cluster timestamp unit)
 *   0x4D80     = MuxingApp
 *   0x5741     = WritingApp
 *   0x1654AE6B = Tracks (master)
 *   0xAE       = TrackEntry (master)
 *   0xD7       = TrackNumber
 *   0x73C5     = TrackUID
 *   0x83       = TrackType (1=video, 2=audio)
 *   0x86       = CodecID
 *   0xE0       = Video (master)
 *   0xB0       = PixelWidth
 *   0xBA       = PixelHeight
 *   0xE1       = Audio (master)
 *   0xB5       = SamplingFrequency
 *   0x9F       = Channels
 *   0x1F43B675 = Cluster (master)
 *   0xE7       = Timestamp (cluster timestamp)
 *   0xA3       = SimpleBlock
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "webm_muxer.h"
#include <cstring>
#include <algorithm>

namespace mc1 {

WebmMuxer::WebmMuxer() = default;

// ── Public API ──────────────────────────────────────────────────────────

void WebmMuxer::write_header(int width, int height, int fps,
                              const std::string& codec_id,
                              bool has_audio, int sample_rate, int channels)
{
    write_ebml_header();
    write_segment_start();
    write_info(fps);
    write_tracks(width, height, codec_id, has_audio, sample_rate, channels);
    header_written_ = true;
}

void WebmMuxer::write_video(const uint8_t* data, size_t len,
                             uint32_t timestamp_ms, bool is_keyframe)
{
    if (!header_written_) return;

    /* Start new cluster on keyframe or when duration exceeds threshold */
    if (!cluster_open_ || is_keyframe ||
        (timestamp_ms - cluster_ts_ms_ >= kClusterDurationMs)) {
        if (cluster_open_) close_cluster();
        open_cluster(timestamp_ms);
    }

    write_simple_block(1, data, len, timestamp_ms, is_keyframe);
    last_ts_ms_ = timestamp_ms;
}

void WebmMuxer::write_audio(const uint8_t* data, size_t len,
                             uint32_t timestamp_ms)
{
    if (!header_written_) return;

    /* Audio can only be written inside a cluster (started by video keyframe) */
    if (!cluster_open_) return;

    write_simple_block(2, data, len, timestamp_ms, false);
}

void WebmMuxer::flush_cluster()
{
    if (cluster_open_) {
        close_cluster();
    }
}

// ── EBML structure writers ──────────────────────────────────────────────

void WebmMuxer::write_ebml_header()
{
    /* Pre-build EBML header content, then wrap in master element */
    std::vector<uint8_t> hdr;
    auto saved = output_;

    /* Capture content to measure size */
    std::vector<uint8_t> content;
    output_ = [&content](const uint8_t* d, size_t l) {
        content.insert(content.end(), d, d + l);
    };

    write_ebml_uint(0x4286, 1);        /* EBMLVersion = 1 */
    write_ebml_uint(0x42F7, 1);        /* EBMLReadVersion = 1 */
    write_ebml_uint(0x42F2, 4);        /* EBMLMaxIDLength = 4 */
    write_ebml_uint(0x42F3, 8);        /* EBMLMaxSizeLength = 8 */
    write_ebml_string(0x4282, "webm"); /* DocType = "webm" */
    write_ebml_uint(0x4287, 4);        /* DocTypeVersion = 4 */
    write_ebml_uint(0x4285, 2);        /* DocTypeReadVersion = 2 */

    output_ = saved;

    /* Write EBML master element: ID + size + content */
    write_ebml_id(0x1A45DFA3);
    write_ebml_size(content.size());
    emit_bytes(content.data(), content.size());
}

void WebmMuxer::write_segment_start()
{
    /* Segment with unknown size (live streaming — never closed) */
    write_ebml_id(0x18538067);
    write_ebml_size_unknown();
}

void WebmMuxer::write_info(int fps)
{
    std::vector<uint8_t> content;
    auto saved = output_;
    output_ = [&content](const uint8_t* d, size_t l) {
        content.insert(content.end(), d, d + l);
    };

    write_ebml_uint(0x2AD7B1, 1000000); /* TimestampScale = 1ms (1,000,000 ns) */
    write_ebml_string(0x4D80, "Mcaster1DSPEncoder");  /* MuxingApp */
    write_ebml_string(0x5741, "Mcaster1DSPEncoder");  /* WritingApp */

    /* Duration hint (optional — use 0 for live) */
    (void)fps;

    output_ = saved;
    write_ebml_id(0x1549A966); /* Info */
    write_ebml_size(content.size());
    emit_bytes(content.data(), content.size());
}

void WebmMuxer::write_tracks(int width, int height, const std::string& codec_id,
                              bool has_audio, int sample_rate, int channels)
{
    /* Build Tracks content */
    std::vector<uint8_t> tracks_content;
    auto saved = output_;
    output_ = [&tracks_content](const uint8_t* d, size_t l) {
        tracks_content.insert(tracks_content.end(), d, d + l);
    };

    /* Track 1: Video */
    {
        std::vector<uint8_t> video_settings;
        auto saved2 = output_;
        output_ = [&video_settings](const uint8_t* d, size_t l) {
            video_settings.insert(video_settings.end(), d, d + l);
        };
        write_ebml_uint(0xB0, static_cast<uint64_t>(width));   /* PixelWidth */
        write_ebml_uint(0xBA, static_cast<uint64_t>(height));  /* PixelHeight */
        output_ = saved2;

        std::vector<uint8_t> track_entry;
        auto saved3 = output_;
        output_ = [&track_entry](const uint8_t* d, size_t l) {
            track_entry.insert(track_entry.end(), d, d + l);
        };
        write_ebml_uint(0xD7, 1);            /* TrackNumber = 1 */
        write_ebml_uint(0x73C5, 1);          /* TrackUID = 1 */
        write_ebml_uint(0x83, 1);            /* TrackType = video */
        write_ebml_string(0x86, codec_id);   /* CodecID = "V_VP8" / "V_VP9" */

        /* Video master element */
        write_ebml_id(0xE0);
        write_ebml_size(video_settings.size());
        emit_bytes(video_settings.data(), video_settings.size());

        output_ = saved3;

        /* TrackEntry master element */
        write_ebml_id(0xAE);
        write_ebml_size(track_entry.size());
        emit_bytes(track_entry.data(), track_entry.size());
    }

    /* Track 2: Audio (optional) */
    if (has_audio) {
        std::vector<uint8_t> audio_settings;
        auto saved2 = output_;
        output_ = [&audio_settings](const uint8_t* d, size_t l) {
            audio_settings.insert(audio_settings.end(), d, d + l);
        };
        write_ebml_float(0xB5, static_cast<double>(sample_rate)); /* SamplingFrequency */
        write_ebml_uint(0x9F, static_cast<uint64_t>(channels));   /* Channels */
        output_ = saved2;

        std::vector<uint8_t> track_entry;
        auto saved3 = output_;
        output_ = [&track_entry](const uint8_t* d, size_t l) {
            track_entry.insert(track_entry.end(), d, d + l);
        };
        write_ebml_uint(0xD7, 2);               /* TrackNumber = 2 */
        write_ebml_uint(0x73C5, 2);             /* TrackUID = 2 */
        write_ebml_uint(0x83, 2);               /* TrackType = audio */
        write_ebml_string(0x86, "A_VORBIS");    /* CodecID (Vorbis in WebM) */

        /* Audio master element */
        write_ebml_id(0xE1);
        write_ebml_size(audio_settings.size());
        emit_bytes(audio_settings.data(), audio_settings.size());

        output_ = saved3;

        write_ebml_id(0xAE);
        write_ebml_size(track_entry.size());
        emit_bytes(track_entry.data(), track_entry.size());
    }

    output_ = saved;

    /* Tracks master element */
    write_ebml_id(0x1654AE6B);
    write_ebml_size(tracks_content.size());
    emit_bytes(tracks_content.data(), tracks_content.size());
}

void WebmMuxer::open_cluster(uint32_t timestamp_ms)
{
    cluster_ts_ms_ = timestamp_ms;
    cluster_open_ = true;

    /* Cluster with unknown size (closed when next cluster starts or on flush) */
    write_ebml_id(0x1F43B675);
    write_ebml_size_unknown();

    /* Cluster Timestamp */
    write_ebml_uint(0xE7, timestamp_ms);
}

void WebmMuxer::close_cluster()
{
    /* In live streaming, clusters are "unknown size" — closing is implicit
     * when the next Cluster element starts. We just mark it closed. */
    cluster_open_ = false;
}

void WebmMuxer::write_simple_block(uint8_t track_num, const uint8_t* data, size_t len,
                                    uint32_t timestamp_ms, bool is_keyframe)
{
    /* SimpleBlock format:
     *   Element ID (0xA3) + EBML size + track_num (EBML coded) +
     *   relative_timestamp (2 bytes, signed) + flags (1 byte) + frame data */

    int16_t rel_ts = static_cast<int16_t>(timestamp_ms - cluster_ts_ms_);
    uint8_t flags = is_keyframe ? 0x80 : 0x00; /* bit 0 of flags = keyframe */

    /* Track number in EBML variable-width encoding (1 byte for track 1-126) */
    size_t block_size = 1 + 2 + 1 + len; /* track(1) + ts(2) + flags(1) + data */

    write_ebml_id(0xA3); /* SimpleBlock */
    write_ebml_size(block_size);

    /* Track number (EBML coded — 0x81 = track 1, 0x82 = track 2) */
    uint8_t hdr[4];
    hdr[0] = 0x80 | track_num;
    hdr[1] = static_cast<uint8_t>(rel_ts >> 8);
    hdr[2] = static_cast<uint8_t>(rel_ts);
    hdr[3] = flags;
    emit_bytes(hdr, 4);
    emit_bytes(data, len);
}

// ── Low-level EBML helpers ──────────────────────────────────────────────

void WebmMuxer::write_ebml_id(uint32_t id)
{
    /* EBML IDs are 1-4 bytes, big-endian, with leading 1-bits for length.
     * We detect the byte count from the ID value itself. */
    if (id <= 0xFF) {
        uint8_t b = static_cast<uint8_t>(id);
        emit_bytes(&b, 1);
    } else if (id <= 0xFFFF) {
        uint8_t b[2] = {
            static_cast<uint8_t>(id >> 8),
            static_cast<uint8_t>(id)
        };
        emit_bytes(b, 2);
    } else if (id <= 0xFFFFFF) {
        uint8_t b[3] = {
            static_cast<uint8_t>(id >> 16),
            static_cast<uint8_t>(id >> 8),
            static_cast<uint8_t>(id)
        };
        emit_bytes(b, 3);
    } else {
        uint8_t b[4] = {
            static_cast<uint8_t>(id >> 24),
            static_cast<uint8_t>(id >> 16),
            static_cast<uint8_t>(id >> 8),
            static_cast<uint8_t>(id)
        };
        emit_bytes(b, 4);
    }
}

void WebmMuxer::write_ebml_size(uint64_t size)
{
    /* EBML variable-length size encoding (VINT):
     * 1 byte:  0b1xxxxxxx (0-127)
     * 2 bytes: 0b01xxxxxx xxxxxxxx (128-16383)
     * etc. */
    size_t bytes = ebml_uint_size(size);
    /* Set the leading length marker bit */
    uint64_t coded = size | (1ULL << (bytes * 7));
    ebml_encode_uint(coded, bytes);
}

void WebmMuxer::write_ebml_size_unknown()
{
    /* 8-byte "unknown" size: all bits set except length marker */
    uint8_t unk[8] = { 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    emit_bytes(unk, 8);
}

void WebmMuxer::write_ebml_uint(uint32_t id, uint64_t val)
{
    write_ebml_id(id);
    size_t bytes = ebml_uint_size(val);
    write_ebml_size(bytes);
    /* Write val as big-endian in 'bytes' bytes */
    for (int i = static_cast<int>(bytes) - 1; i >= 0; --i) {
        uint8_t b = static_cast<uint8_t>(val >> (i * 8));
        emit_bytes(&b, 1);
    }
}

void WebmMuxer::write_ebml_float(uint32_t id, double val)
{
    write_ebml_id(id);
    write_ebml_size(8); /* 8-byte float */
    uint64_t bits;
    std::memcpy(&bits, &val, 8);
    /* Big-endian */
    for (int i = 56; i >= 0; i -= 8) {
        uint8_t b = static_cast<uint8_t>(bits >> i);
        emit_bytes(&b, 1);
    }
}

void WebmMuxer::write_ebml_string(uint32_t id, const std::string& val)
{
    write_ebml_id(id);
    write_ebml_size(val.size());
    emit_bytes(reinterpret_cast<const uint8_t*>(val.data()), val.size());
}

void WebmMuxer::write_ebml_binary(uint32_t id, const uint8_t* data, size_t len)
{
    write_ebml_id(id);
    write_ebml_size(len);
    emit_bytes(data, len);
}

size_t WebmMuxer::ebml_uint_size(uint64_t val)
{
    if (val < 0x80) return 1;
    if (val < 0x4000) return 2;
    if (val < 0x200000) return 3;
    if (val < 0x10000000) return 4;
    if (val < 0x0800000000ULL) return 5;
    if (val < 0x040000000000ULL) return 6;
    if (val < 0x02000000000000ULL) return 7;
    return 8;
}

void WebmMuxer::ebml_encode_uint(uint64_t val, size_t bytes)
{
    for (int i = static_cast<int>(bytes) - 1; i >= 0; --i) {
        uint8_t b = static_cast<uint8_t>(val >> (i * 8));
        emit_bytes(&b, 1);
    }
}

void WebmMuxer::emit_bytes(const uint8_t* data, size_t len)
{
    if (output_ && len > 0)
        output_(data, len);
}

} // namespace mc1
