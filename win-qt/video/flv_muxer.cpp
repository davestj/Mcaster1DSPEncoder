/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/flv_muxer.cpp — FLV container muxer implementation
 *
 * FLV specification: Adobe Flash Video File Format Specification Version 10.1
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "flv_muxer.h"
#include <cstring>
#include <cstdio>

namespace mc1 {

FlvMuxer::FlvMuxer() = default;

void FlvMuxer::put_be16(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

void FlvMuxer::put_be24(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 16);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v);
}

void FlvMuxer::put_be32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

void FlvMuxer::emit_bytes(const uint8_t* data, size_t len)
{
    if (output_) output_(data, len);
}

void FlvMuxer::write_header(bool has_audio, bool has_video)
{
    uint8_t header[13];

    /* FLV signature */
    header[0] = 'F';
    header[1] = 'L';
    header[2] = 'V';
    header[3] = 0x01; /* version */

    /* Type flags */
    uint8_t flags = 0;
    if (has_audio) flags |= 0x04;
    if (has_video) flags |= 0x01;
    header[4] = flags;

    /* Data offset (header size) */
    put_be32(header + 5, 9);

    /* PreviousTagSize0 (always 0) */
    put_be32(header + 9, 0);

    emit_bytes(header, sizeof(header));
    prev_tag_size_ = 0;
}

void FlvMuxer::write_tag(uint8_t tag_type, const uint8_t* data, size_t data_len,
                           uint32_t timestamp_ms)
{
    /* FLV tag header: 11 bytes */
    uint8_t tag_header[11];

    tag_header[0] = tag_type;
    put_be24(tag_header + 1, static_cast<uint32_t>(data_len));

    /* Timestamp (lower 24 bits + extension byte) */
    put_be24(tag_header + 4, timestamp_ms & 0x00FFFFFF);
    tag_header[7] = static_cast<uint8_t>((timestamp_ms >> 24) & 0xFF);

    /* StreamID (always 0) */
    put_be24(tag_header + 8, 0);

    emit_bytes(tag_header, sizeof(tag_header));
    emit_bytes(data, data_len);

    /* PreviousTagSize */
    uint32_t prev = static_cast<uint32_t>(11 + data_len);
    uint8_t prev_buf[4];
    put_be32(prev_buf, prev);
    emit_bytes(prev_buf, 4);

    prev_tag_size_ = prev;
}

void FlvMuxer::write_avc_sequence_header(const uint8_t* sps, size_t sps_len,
                                           const uint8_t* pps, size_t pps_len,
                                           uint32_t timestamp_ms)
{
    /* VideoTagHeader (5 bytes) + AVCDecoderConfigurationRecord */
    std::vector<uint8_t> payload;
    payload.reserve(5 + 11 + sps_len + 3 + pps_len);

    /* VideoTagHeader */
    payload.push_back(0x17); /* keyframe (1) + AVC (7) */
    payload.push_back(0x00); /* AVC sequence header */
    payload.push_back(0x00); /* composition time offset */
    payload.push_back(0x00);
    payload.push_back(0x00);

    /* AVCDecoderConfigurationRecord */
    payload.push_back(0x01); /* version */
    payload.push_back(sps_len > 1 ? sps[1] : 0x64); /* profile */
    payload.push_back(sps_len > 2 ? sps[2] : 0x00); /* profile compat */
    payload.push_back(sps_len > 3 ? sps[3] : 0x1F); /* level */
    payload.push_back(0xFF); /* 6 bits reserved + 2 bits NALU length size - 1 (= 3) */

    /* SPS */
    payload.push_back(0xE1); /* 3 bits reserved + 5 bits num SPS (= 1) */
    uint8_t sps_len_be[2];
    put_be16(sps_len_be, static_cast<uint16_t>(sps_len));
    payload.insert(payload.end(), sps_len_be, sps_len_be + 2);
    payload.insert(payload.end(), sps, sps + sps_len);

    /* PPS */
    payload.push_back(0x01); /* num PPS */
    uint8_t pps_len_be[2];
    put_be16(pps_len_be, static_cast<uint16_t>(pps_len));
    payload.insert(payload.end(), pps_len_be, pps_len_be + 2);
    payload.insert(payload.end(), pps, pps + pps_len);

    write_tag(0x09, payload.data(), payload.size(), timestamp_ms);
}

void FlvMuxer::write_video(const uint8_t* nalu_data, size_t nalu_len,
                             uint32_t timestamp_ms, bool is_keyframe)
{
    /* VideoTagHeader (5 bytes) + NALU data */
    std::vector<uint8_t> payload;
    payload.reserve(5 + nalu_len);

    /* Frame type (1=keyframe, 2=inter) + codec (7=AVC) */
    payload.push_back(is_keyframe ? 0x17 : 0x27);
    payload.push_back(0x01); /* AVC NALU */
    payload.push_back(0x00); /* composition time offset */
    payload.push_back(0x00);
    payload.push_back(0x00);

    payload.insert(payload.end(), nalu_data, nalu_data + nalu_len);

    write_tag(0x09, payload.data(), payload.size(), timestamp_ms);
}

void FlvMuxer::write_aac_sequence_header(const uint8_t* asc, size_t asc_len,
                                           uint32_t timestamp_ms)
{
    /* AudioTagHeader (2 bytes) + AudioSpecificConfig */
    std::vector<uint8_t> payload;
    payload.reserve(2 + asc_len);

    /* SoundFormat=AAC(10), SampleRate=44100(3), SampleSize=16bit(1), Stereo(1) */
    payload.push_back(0xAF); /* AAC, 44100, 16-bit, stereo */
    payload.push_back(0x00); /* AAC sequence header */
    payload.insert(payload.end(), asc, asc + asc_len);

    write_tag(0x08, payload.data(), payload.size(), timestamp_ms);
}

void FlvMuxer::write_aac_audio(const uint8_t* frame, size_t frame_len,
                                 uint32_t timestamp_ms)
{
    std::vector<uint8_t> payload;
    payload.reserve(2 + frame_len);

    payload.push_back(0xAF); /* AAC, 44100, 16-bit, stereo */
    payload.push_back(0x01); /* AAC raw */
    payload.insert(payload.end(), frame, frame + frame_len);

    write_tag(0x08, payload.data(), payload.size(), timestamp_ms);
}

void FlvMuxer::write_mp3_audio(const uint8_t* frame, size_t frame_len,
                                 uint32_t timestamp_ms, int sample_rate, bool stereo)
{
    std::vector<uint8_t> payload;
    payload.reserve(1 + frame_len);

    /* SoundFormat=MP3(2), sample rate, 16-bit, stereo/mono */
    uint8_t header = 0x20; /* MP3 */
    if (sample_rate >= 44100)      header |= 0x0C; /* 44100 Hz */
    else if (sample_rate >= 22050) header |= 0x08; /* 22050 Hz */
    else if (sample_rate >= 11025) header |= 0x04; /* 11025 Hz */
    header |= 0x02; /* 16-bit */
    if (stereo) header |= 0x01;

    payload.push_back(header);
    payload.insert(payload.end(), frame, frame + frame_len);

    write_tag(0x08, payload.data(), payload.size(), timestamp_ms);
}

} // namespace mc1
