/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/amf0.cpp — Minimal AMF0 binary encoder for RTMP commands
 *
 * Reference: AMF0 specification (amf0-file-format-specification.pdf)
 *
 * Type markers:
 *   0x00 = number (IEEE 754 double, 8 bytes big-endian)
 *   0x01 = boolean (1 byte)
 *   0x02 = string (2-byte BE length + UTF-8)
 *   0x03 = object start
 *   0x05 = null
 *   0x08 = ECMA array (4-byte count + properties)
 *   0x00 0x00 0x09 = object end marker
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "amf0.h"
#include <cstring>

namespace mc1 {

// ── Helpers ─────────────────────────────────────────────────────────────

void Amf0Writer::put_u8(uint8_t v)
{
    buf_.push_back(v);
}

void Amf0Writer::put_be16(uint16_t v)
{
    buf_.push_back(static_cast<uint8_t>(v >> 8));
    buf_.push_back(static_cast<uint8_t>(v));
}

void Amf0Writer::put_be32(uint32_t v)
{
    buf_.push_back(static_cast<uint8_t>(v >> 24));
    buf_.push_back(static_cast<uint8_t>(v >> 16));
    buf_.push_back(static_cast<uint8_t>(v >> 8));
    buf_.push_back(static_cast<uint8_t>(v));
}

void Amf0Writer::put_be64(uint64_t v)
{
    for (int i = 56; i >= 0; i -= 8)
        buf_.push_back(static_cast<uint8_t>(v >> i));
}

void Amf0Writer::write_string_payload(const std::string& val)
{
    put_be16(static_cast<uint16_t>(val.size()));
    buf_.insert(buf_.end(), val.begin(), val.end());
}

// ── Primitive types ─────────────────────────────────────────────────────

void Amf0Writer::write_number(double val)
{
    put_u8(0x00); // number marker
    uint64_t bits;
    std::memcpy(&bits, &val, 8);
    put_be64(bits);
}

void Amf0Writer::write_boolean(bool val)
{
    put_u8(0x01); // boolean marker
    put_u8(val ? 0x01 : 0x00);
}

void Amf0Writer::write_string(const std::string& val)
{
    put_u8(0x02); // string marker
    write_string_payload(val);
}

void Amf0Writer::write_null()
{
    put_u8(0x05); // null marker
}

// ── Object type ─────────────────────────────────────────────────────────

void Amf0Writer::begin_object()
{
    put_u8(0x03); // object marker
}

void Amf0Writer::write_property(const std::string& key, double val)
{
    write_string_payload(key); // property name (no type marker)
    write_number(val);
}

void Amf0Writer::write_property(const std::string& key, const std::string& val)
{
    write_string_payload(key);
    write_string(val);
}

void Amf0Writer::write_property(const std::string& key, bool val)
{
    write_string_payload(key);
    write_boolean(val);
}

void Amf0Writer::end_object()
{
    /* Object end marker: 0x00 0x00 (empty property name) + 0x09 */
    put_be16(0x0000);
    put_u8(0x09);
}

// ── ECMA array ──────────────────────────────────────────────────────────

void Amf0Writer::begin_ecma_array(uint32_t count)
{
    put_u8(0x08); // ECMA array marker
    put_be32(count);
}

} // namespace mc1
