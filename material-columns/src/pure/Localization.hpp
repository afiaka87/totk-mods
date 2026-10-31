// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace material_columns::localization {
inline constexpr std::size_t capacity = 2048;
inline constexpr const char* locales[]{"ja", "en-US", "fr", "de", "it", "es", "ko", "nl",
    "pt", "ru", "en-GB", "fr-CA", "es-419", "zh-Hans", "zh-Hant", "pt-BR"};
inline const char* select(const char* code) {
    if (code) {
        for (const auto* locale : locales) if (std::strncmp(code, locale, 8) == 0) return locale;
        if (std::strncmp(code, "zh-CN", 8) == 0) return "zh-Hans";
        if (std::strncmp(code, "zh-TW", 8) == 0) return "zh-Hant";
    }
    return "en-US";
}

// Reject malformed UTF-8 and overflow instead of passing partial glyphs to the native textbox.
inline bool decode(const char* source, std::size_t bytes, char16_t (&out)[64], unsigned& length) {
    length = 0;
    for (std::size_t i=0; i<bytes;) {
        const auto first = static_cast<unsigned char>(source[i++]);
        if (!first) { out[length] = 0; return true; }
        unsigned value = first, continuation = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { value &= 0x1f; continuation=1; minimum=0x80; }
        else if (first >= 0xe0 && first <= 0xef) { value &= 0x0f; continuation=2; minimum=0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { value &= 7; continuation=3; minimum=0x10000; }
        else if (first >= 0x80 || first < 0x20) return false;
        if (continuation > bytes-i) return false;
        for (unsigned n=0; n<continuation; ++n) {
            const auto next = static_cast<unsigned char>(source[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
        if (length + (value > 0xffff ? 2 : 1) >= 64) return false;
        if (value > 0xffff) { value -= 0x10000; out[length++] = char16_t(0xd800 + (value >> 10)); value = 0xdc00 + (value & 0x3ff); }
        out[length++] = char16_t(value);
    }
    return false;
}

class Pack {
    const unsigned char* data_{};
    unsigned count_{};
    static std::uint32_t u32(const unsigned char* p) {
        return std::uint32_t(p[0]) | std::uint32_t(p[1])<<8 | std::uint32_t(p[2])<<16 | std::uint32_t(p[3])<<24;
    }
    static std::uint64_t id(const unsigned char* p) { return u32(p) | std::uint64_t(u32(p+4))<<32; }
public:
    bool load(const unsigned char* data, std::size_t size) {
        data_ = nullptr; count_ = 0;
        if (!data || size < 8 || size > capacity || std::memcmp(data,"MCL1",4)) return false;
        const auto count = u32(data+4);
        if (!count || count > 96 || 8 + count*12 > size) return false;
        std::size_t next = 8 + count*12;
        std::uint64_t previous = 0;
        for (unsigned i=0; i<count; ++i) {
            const auto* entry = data+8+i*12;
            const auto key = id(entry);
            const auto offset = u32(entry+8);
            if (!key || (i && key <= previous) || offset != next || offset >= size) return false;
            char16_t text[64]; unsigned length;
            if (!decode(reinterpret_cast<const char*>(data+offset), size-offset, text, length) || !length) return false;
            next = offset + std::strlen(reinterpret_cast<const char*>(data+offset)) + 1;
            previous = key;
        }
        if (next != size) return false;
        data_ = data; count_ = count;
        return true;
    }
    const char* label(std::uint64_t key, const char* fallback) const {
        unsigned first=0, end=count_;
        while (first<end) {
            const auto middle=first+(end-first)/2;
            if (id(data_+8+middle*12)<key) first=middle+1; else end=middle;
        }
        const auto* entry = data_ ? data_+8+first*12 : nullptr;
        return first<count_ && id(entry)==key ? reinterpret_cast<const char*>(data_+u32(entry+8)) : fallback;
    }
};
}
