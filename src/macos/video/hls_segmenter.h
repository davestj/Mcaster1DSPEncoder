/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/hls_segmenter.h — HLS MPEG-TS segmenter with M3U8 playlist
 *
 * Writes H.264/AAC into MPEG-TS segments (.ts files) and maintains
 * a rolling M3U8 playlist. Segments are split on keyframes at a
 * configurable duration (default 6 seconds).
 *
 * Output directory structure:
 *   output_dir/
 *     playlist.m3u8
 *     segment_000000.ts
 *     segment_000001.ts
 *     ...
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_HLS_SEGMENTER_H
#define MC1_HLS_SEGMENTER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <fstream>

namespace mc1 {

class HlsSegmenter {
public:
    HlsSegmenter();
    ~HlsSegmenter();

    /* Initialize the segmenter.
     * output_dir: directory for .ts segments and .m3u8 playlist.
     * target_duration_sec: target segment length (split on next keyframe after this).
     * max_segments: rolling window of segments to keep (0 = keep all). */
    bool init(const std::string& output_dir, int target_duration_sec = 6,
              int max_segments = 5);

    /* Set video parameters (needed for PAT/PMT + PES headers) */
    void set_video_params(int width, int height, int fps);

    /* Set audio parameters */
    void set_audio_params(int sample_rate, int channels);

    /* Write H.264 NALU data (Annex B format with start codes) */
    void write_video(const uint8_t* nalu, size_t len,
                     int64_t pts_us, bool is_keyframe);

    /* Write AAC audio frame (raw AAC without ADTS) */
    void write_audio(const uint8_t* frame, size_t len, int64_t pts_us);

    /* Close the current segment and finalize the playlist */
    void close();

    /* Stats */
    int segments_written() const { return segment_index_; }

private:
    /* MPEG-TS packet construction */
    void write_pat();
    void write_pmt();
    void write_pes(uint16_t pid, uint8_t stream_id,
                   const uint8_t* data, size_t len, int64_t pts_us);
    void write_ts_packet(uint16_t pid, bool payload_start,
                         const uint8_t* payload, size_t payload_len,
                         const uint8_t* adaptation, size_t adaptation_len);

    /* Segment management */
    void start_segment();
    void end_segment();
    void update_playlist();
    void delete_old_segments();
    std::string segment_filename(int index) const;

    /* PTS encoding into PES header */
    static void encode_pts(uint8_t* buf, uint8_t marker, int64_t pts);

    std::string output_dir_;
    int target_duration_sec_ = 6;
    int max_segments_        = 5;

    int width_       = 1280;
    int height_      = 720;
    int fps_         = 30;
    int sample_rate_ = 44100;
    int channels_    = 2;

    std::ofstream segment_file_;
    int           segment_index_   = 0;
    int64_t       segment_start_pts_ = -1;
    bool          segment_open_    = false;
    bool          initialized_     = false;

    /* Continuity counters (4-bit, wraps at 16) */
    uint8_t cc_pat_   = 0;
    uint8_t cc_pmt_   = 0;
    uint8_t cc_video_ = 0;
    uint8_t cc_audio_ = 0;

    /* Segment duration tracking for playlist */
    struct SegmentInfo {
        std::string filename;
        double      duration_sec;
    };
    std::vector<SegmentInfo> segments_;

    static constexpr uint16_t kPidPat   = 0x0000;
    static constexpr uint16_t kPidPmt   = 0x1000;
    static constexpr uint16_t kPidVideo = 0x0100;
    static constexpr uint16_t kPidAudio = 0x0101;
    static constexpr int      kTsPacketSize = 188;
};

} // namespace mc1

#endif // MC1_HLS_SEGMENTER_H
