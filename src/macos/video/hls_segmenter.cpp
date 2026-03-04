/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/hls_segmenter.cpp — HLS MPEG-TS segmenter with M3U8 playlist
 *
 * MPEG-TS packet format (188 bytes):
 *   [4-byte header] [optional adaptation field] [payload]
 *   Header: sync(0x47) + PID(13 bits) + flags + continuity counter(4 bits)
 *
 * PES (Packetized Elementary Stream) wraps audio/video frames:
 *   [3-byte start code: 00 00 01] [stream_id] [packet_length(2)] [flags] [PTS]
 *
 * PAT (Program Association Table): PID 0x0000
 * PMT (Program Map Table): PID 0x1000
 * Video elementary stream: PID 0x0100 (stream_id 0xE0 = video)
 * Audio elementary stream: PID 0x0101 (stream_id 0xC0 = audio)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hls_segmenter.h"
#include <cstring>
#include <cstdio>
#include <filesystem>

namespace mc1 {

HlsSegmenter::HlsSegmenter() = default;

HlsSegmenter::~HlsSegmenter()
{
    close();
}

bool HlsSegmenter::init(const std::string& output_dir, int target_duration_sec,
                          int max_segments)
{
    output_dir_ = output_dir;
    target_duration_sec_ = target_duration_sec;
    max_segments_ = max_segments;
    segment_index_ = 0;
    segments_.clear();

    /* Ensure output directory exists */
    std::error_code ec;
    std::filesystem::create_directories(output_dir_, ec);
    if (ec) return false;

    initialized_ = true;
    return true;
}

void HlsSegmenter::set_video_params(int width, int height, int fps)
{
    width_ = width;
    height_ = height;
    fps_ = fps;
}

void HlsSegmenter::set_audio_params(int sample_rate, int channels)
{
    sample_rate_ = sample_rate;
    channels_ = channels;
}

void HlsSegmenter::write_video(const uint8_t* nalu, size_t len,
                                 int64_t pts_us, bool is_keyframe)
{
    if (!initialized_) return;

    /* Check if we should start a new segment */
    if (is_keyframe && segment_open_ && segment_start_pts_ >= 0) {
        double elapsed_sec = static_cast<double>(pts_us - segment_start_pts_) / 1000000.0;
        if (elapsed_sec >= target_duration_sec_) {
            end_segment();
        }
    }

    if (!segment_open_) {
        start_segment();
        segment_start_pts_ = pts_us;
    }

    /* Convert PTS from microseconds to 90kHz MPEG-TS clock */
    int64_t pts_90k = (pts_us * 90) / 1000;

    write_pes(kPidVideo, 0xE0, nalu, len, pts_90k);
}

void HlsSegmenter::write_audio(const uint8_t* frame, size_t len, int64_t pts_us)
{
    if (!initialized_ || !segment_open_) return;

    int64_t pts_90k = (pts_us * 90) / 1000;
    write_pes(kPidAudio, 0xC0, frame, len, pts_90k);
}

void HlsSegmenter::close()
{
    if (segment_open_) {
        end_segment();
    }
    if (initialized_) {
        update_playlist();
        initialized_ = false;
    }
}

// ── MPEG-TS packet construction ─────────────────────────────────────────

void HlsSegmenter::write_pat()
{
    /* PAT payload: table_id(1) + flags(2) + TSid(2) + version(1) +
     * section(1) + last_section(1) + program_num(2) + PMT_PID(2) + CRC32(4) */
    uint8_t pat[17];
    std::memset(pat, 0, sizeof(pat));

    pat[0] = 0x00;  /* table_id = 0 (PAT) */
    /* section_syntax_indicator=1, zero=0, reserved=11, section_length=13 */
    pat[1] = 0xB0;
    pat[2] = 0x0D;  /* section_length = 13 */
    pat[3] = 0x00;  /* transport_stream_id high */
    pat[4] = 0x01;  /* transport_stream_id low */
    pat[5] = 0xC1;  /* reserved=11, version=0, current_next=1 */
    pat[6] = 0x00;  /* section_number */
    pat[7] = 0x00;  /* last_section_number */
    pat[8] = 0x00;  /* program_number high */
    pat[9] = 0x01;  /* program_number low = 1 */
    pat[10] = 0xF0 | static_cast<uint8_t>((kPidPmt >> 8) & 0x1F); /* reserved + PMT PID high */
    pat[11] = static_cast<uint8_t>(kPidPmt & 0xFF);                /* PMT PID low */

    /* CRC32/MPEG2 over bytes 0..11 */
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < 12; ++i) {
        uint8_t byte = pat[i];
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc ^ (static_cast<uint32_t>(byte) << 24)) & 0x80000000)
                crc = (crc << 1) ^ 0x04C11DB7;
            else
                crc <<= 1;
            byte <<= 1;
        }
    }
    pat[12] = static_cast<uint8_t>(crc >> 24);
    pat[13] = static_cast<uint8_t>(crc >> 16);
    pat[14] = static_cast<uint8_t>(crc >> 8);
    pat[15] = static_cast<uint8_t>(crc);
    pat[16] = 0xFF; /* stuffing */

    /* We need a pointer_field (0x00) before the table */
    uint8_t payload[18];
    payload[0] = 0x00; /* pointer_field */
    std::memcpy(payload + 1, pat, 16);

    write_ts_packet(kPidPat, true, payload, 17, nullptr, 0);
    cc_pat_ = (cc_pat_ + 1) & 0x0F;
}

void HlsSegmenter::write_pmt()
{
    /* PMT for program 1: video PID 0x0100 (H.264), audio PID 0x0101 (AAC) */
    uint8_t pmt[32];
    std::memset(pmt, 0, sizeof(pmt));

    int idx = 0;
    pmt[idx++] = 0x02;  /* table_id = 2 (PMT) */
    /* section_syntax=1, reserved, section_length — fill in after */
    int len_pos = idx;
    idx += 2;
    pmt[idx++] = 0x00;  /* program_number high */
    pmt[idx++] = 0x01;  /* program_number low = 1 */
    pmt[idx++] = 0xC1;  /* reserved, version=0, current_next=1 */
    pmt[idx++] = 0x00;  /* section_number */
    pmt[idx++] = 0x00;  /* last_section_number */
    pmt[idx++] = 0xE0 | static_cast<uint8_t>((kPidVideo >> 8) & 0x1F); /* PCR PID high */
    pmt[idx++] = static_cast<uint8_t>(kPidVideo & 0xFF);                /* PCR PID low */
    pmt[idx++] = 0xF0;  /* reserved + program_info_length high */
    pmt[idx++] = 0x00;  /* program_info_length low = 0 */

    /* Video stream: stream_type=0x1B (H.264) */
    pmt[idx++] = 0x1B;  /* stream_type */
    pmt[idx++] = 0xE0 | static_cast<uint8_t>((kPidVideo >> 8) & 0x1F);
    pmt[idx++] = static_cast<uint8_t>(kPidVideo & 0xFF);
    pmt[idx++] = 0xF0;  /* ES_info_length high */
    pmt[idx++] = 0x00;  /* ES_info_length low = 0 */

    /* Audio stream: stream_type=0x0F (AAC ADTS) */
    pmt[idx++] = 0x0F;  /* stream_type */
    pmt[idx++] = 0xE0 | static_cast<uint8_t>((kPidAudio >> 8) & 0x1F);
    pmt[idx++] = static_cast<uint8_t>(kPidAudio & 0xFF);
    pmt[idx++] = 0xF0;
    pmt[idx++] = 0x00;

    /* Fill in section_length (includes from program_number to CRC, inclusive) */
    int section_len = idx - 3 + 4; /* +4 for CRC */
    pmt[len_pos]     = 0xB0 | static_cast<uint8_t>((section_len >> 8) & 0x0F);
    pmt[len_pos + 1] = static_cast<uint8_t>(section_len & 0xFF);

    /* CRC32/MPEG2 */
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < idx; ++i) {
        uint8_t byte = pmt[i];
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc ^ (static_cast<uint32_t>(byte) << 24)) & 0x80000000)
                crc = (crc << 1) ^ 0x04C11DB7;
            else
                crc <<= 1;
            byte <<= 1;
        }
    }
    pmt[idx++] = static_cast<uint8_t>(crc >> 24);
    pmt[idx++] = static_cast<uint8_t>(crc >> 16);
    pmt[idx++] = static_cast<uint8_t>(crc >> 8);
    pmt[idx++] = static_cast<uint8_t>(crc);

    uint8_t payload[64];
    payload[0] = 0x00; /* pointer_field */
    std::memcpy(payload + 1, pmt, static_cast<size_t>(idx));

    write_ts_packet(kPidPmt, true, payload, static_cast<size_t>(idx + 1), nullptr, 0);
    cc_pmt_ = (cc_pmt_ + 1) & 0x0F;
}

void HlsSegmenter::write_pes(uint16_t pid, uint8_t stream_id,
                               const uint8_t* data, size_t len, int64_t pts_90k)
{
    /* Build PES packet:
     * start_code(3) + stream_id(1) + packet_length(2) +
     * flags(2) + header_data_length(1) + PTS(5) + payload */
    size_t pes_hdr_len = 3 + 1 + 2 + 2 + 1 + 5;
    size_t pes_total = pes_hdr_len + len;

    std::vector<uint8_t> pes(pes_total);
    pes[0] = 0x00;
    pes[1] = 0x00;
    pes[2] = 0x01;
    pes[3] = stream_id;

    /* PES packet length (0 = unbounded for video, actual for audio) */
    if (stream_id == 0xE0) {
        pes[4] = 0x00;
        pes[5] = 0x00;
    } else {
        size_t pes_pkt_len = len + 5 + 3; /* payload + PTS(5) + flags(2) + hdr_len(1) */
        if (pes_pkt_len > 0xFFFF) pes_pkt_len = 0;
        pes[4] = static_cast<uint8_t>(pes_pkt_len >> 8);
        pes[5] = static_cast<uint8_t>(pes_pkt_len);
    }

    pes[6] = 0x80;  /* marker bits = 10, no scrambling, no priority, no alignment */
    pes[7] = 0x80;  /* PTS_DTS_flags = 10 (PTS only) */
    pes[8] = 0x05;  /* PES_header_data_length = 5 (PTS) */

    encode_pts(pes.data() + 9, 0x20, pts_90k);

    std::memcpy(pes.data() + pes_hdr_len, data, len);

    /* Split PES into 188-byte TS packets */
    uint8_t& cc = (pid == kPidVideo) ? cc_video_ : cc_audio_;
    size_t offset = 0;
    bool first = true;
    while (offset < pes_total) {
        size_t avail = kTsPacketSize - 4; /* 184 bytes payload per packet */
        size_t chunk = std::min(avail, pes_total - offset);

        write_ts_packet(pid, first, pes.data() + offset, chunk, nullptr, 0);
        cc = (cc + 1) & 0x0F;
        offset += chunk;
        first = false;
    }
}

void HlsSegmenter::write_ts_packet(uint16_t pid, bool payload_start,
                                     const uint8_t* payload, size_t payload_len,
                                     const uint8_t* adaptation, size_t adaptation_len)
{
    uint8_t pkt[kTsPacketSize];
    std::memset(pkt, 0xFF, kTsPacketSize); /* Fill with stuffing bytes */

    /* 4-byte TS header */
    pkt[0] = 0x47; /* sync byte */
    pkt[1] = (payload_start ? 0x40 : 0x00) | static_cast<uint8_t>((pid >> 8) & 0x1F);
    pkt[2] = static_cast<uint8_t>(pid & 0xFF);

    uint8_t& cc = (pid == kPidPat) ? cc_pat_ :
                  (pid == kPidPmt) ? cc_pmt_ :
                  (pid == kPidVideo) ? cc_video_ : cc_audio_;

    size_t hdr_len = 4;

    if (adaptation && adaptation_len > 0) {
        pkt[3] = 0x30 | (cc & 0x0F); /* adaptation + payload */
        pkt[4] = static_cast<uint8_t>(adaptation_len);
        std::memcpy(pkt + 5, adaptation, adaptation_len);
        hdr_len = 5 + adaptation_len;
    } else {
        /* Check if we need stuffing (payload < 184 bytes) */
        size_t space = kTsPacketSize - 4;
        if (payload_len < space) {
            /* Add adaptation field for stuffing */
            size_t stuff = space - payload_len;
            pkt[3] = 0x30 | (cc & 0x0F); /* adaptation + payload */
            if (stuff == 1) {
                pkt[4] = 0x00; /* adaptation_field_length = 0 */
                hdr_len = 5;
            } else {
                pkt[4] = static_cast<uint8_t>(stuff - 1); /* adaptation_field_length */
                pkt[5] = 0x00; /* flags (no PCR etc.) */
                /* Remaining stuffing bytes are already 0xFF */
                hdr_len = 4 + stuff;
            }
        } else {
            pkt[3] = 0x10 | (cc & 0x0F); /* payload only */
            hdr_len = 4;
        }
    }

    /* Copy payload */
    if (payload_len > 0 && payload) {
        size_t copy_len = std::min(payload_len, static_cast<size_t>(kTsPacketSize) - hdr_len);
        std::memcpy(pkt + hdr_len, payload, copy_len);
    }

    if (segment_file_.is_open()) {
        segment_file_.write(reinterpret_cast<const char*>(pkt), kTsPacketSize);
    }
}

// ── Segment management ──────────────────────────────────────────────────

void HlsSegmenter::start_segment()
{
    std::string path = output_dir_ + "/" + segment_filename(segment_index_);
    segment_file_.open(path, std::ios::binary);
    segment_open_ = true;

    /* Write PAT + PMT at the start of every segment */
    write_pat();
    write_pmt();
}

void HlsSegmenter::end_segment()
{
    if (!segment_open_) return;
    segment_file_.close();
    segment_open_ = false;

    /* Calculate segment duration from PTS */
    /* For simplicity, use target_duration as approximate */
    double dur = static_cast<double>(target_duration_sec_);

    segments_.push_back({ segment_filename(segment_index_), dur });
    segment_index_++;

    update_playlist();
    delete_old_segments();
}

void HlsSegmenter::update_playlist()
{
    std::string path = output_dir_ + "/playlist.m3u8";
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return;

    f << "#EXTM3U\n";
    f << "#EXT-X-VERSION:3\n";
    f << "#EXT-X-TARGETDURATION:" << target_duration_sec_ << "\n";

    int start_seq = 0;
    size_t start_idx = 0;
    if (max_segments_ > 0 && segments_.size() > static_cast<size_t>(max_segments_)) {
        start_idx = segments_.size() - static_cast<size_t>(max_segments_);
        start_seq = static_cast<int>(start_idx);
    }

    f << "#EXT-X-MEDIA-SEQUENCE:" << start_seq << "\n";

    for (size_t i = start_idx; i < segments_.size(); ++i) {
        f << "#EXTINF:" << segments_[i].duration_sec << ",\n";
        f << segments_[i].filename << "\n";
    }

    /* Don't write EXT-X-ENDLIST for live streams */
}

void HlsSegmenter::delete_old_segments()
{
    if (max_segments_ <= 0) return;
    while (segments_.size() > static_cast<size_t>(max_segments_ + 2)) {
        /* Keep a buffer of +2 segments beyond the playlist window */
        std::string path = output_dir_ + "/" + segments_.front().filename;
        std::error_code ec;
        std::filesystem::remove(path, ec);
        segments_.erase(segments_.begin());
    }
}

std::string HlsSegmenter::segment_filename(int index) const
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "segment_%06d.ts", index);
    return buf;
}

// ── PTS encoding ────────────────────────────────────────────────────────

void HlsSegmenter::encode_pts(uint8_t* buf, uint8_t marker, int64_t pts)
{
    /* 5-byte PTS field:
     * [4 bits marker][3 bits PTS[32..30]][1 bit marker]
     * [15 bits PTS[29..15]][1 bit marker]
     * [15 bits PTS[14..0]][1 bit marker] */
    buf[0] = static_cast<uint8_t>(
        (marker & 0xF0) |
        (static_cast<uint8_t>((pts >> 29) & 0x0E)) |
        0x01);
    buf[1] = static_cast<uint8_t>((pts >> 22) & 0xFF);
    buf[2] = static_cast<uint8_t>(((pts >> 14) & 0xFE) | 0x01);
    buf[3] = static_cast<uint8_t>((pts >> 7) & 0xFF);
    buf[4] = static_cast<uint8_t>(((pts << 1) & 0xFE) | 0x01);
}

} // namespace mc1
