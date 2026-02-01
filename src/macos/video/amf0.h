/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/amf0.h — Minimal AMF0 binary encoder for RTMP commands
 *
 * AMF0 (Action Message Format version 0) is used by RTMP for
 * command messages like connect, createStream, and publish.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_AMF0_H
#define MC1_AMF0_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace mc1 {

class Amf0Writer {
public:
    Amf0Writer() = default;

    /* Primitive types */
    void write_number(double val);
    void write_boolean(bool val);
    void write_string(const std::string& val);
    void write_null();

    /* Object type (key-value pairs terminated by 0x000009) */
    void begin_object();
    void write_property(const std::string& key, double val);
    void write_property(const std::string& key, const std::string& val);
    void write_property(const std::string& key, bool val);
    void end_object();

    /* ECMA array (like object but with count prefix) */
    void begin_ecma_array(uint32_t count);
    /* Use write_property() for entries, then end_object() to close */

    /* Access the serialized buffer */
    const std::vector<uint8_t>& data() const { return buf_; }
    size_t size() const { return buf_.size(); }

    /* Reset for reuse */
    void clear() { buf_.clear(); }

private:
    void put_u8(uint8_t v);
    void put_be16(uint16_t v);
    void put_be32(uint32_t v);
    void put_be64(uint64_t v);
    void write_string_payload(const std::string& val);

    std::vector<uint8_t> buf_;
};

} // namespace mc1

#endif // MC1_AMF0_H
