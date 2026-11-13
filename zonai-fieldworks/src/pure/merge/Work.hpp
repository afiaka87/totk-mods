// Copyright (c) HanSoloBlaster, afiaka87
// A loaded game file with spare room after it, and the byte helpers the merges share. No game code.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fieldworks::merge {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u32 kNone = 0xffffffffu;

inline u32 rd32(const u8* p, bool big) {
    return big ? (u32{p[0]} << 24) | (u32{p[1]} << 16) | (u32{p[2]} << 8) | p[3]
               : (u32{p[3]} << 24) | (u32{p[2]} << 16) | (u32{p[1]} << 8) | p[0];
}
inline void wr32(u8* p, u32 v, bool big) {
    for (int i = 0; i < 4; ++i) p[big ? 3 - i : i] = static_cast<u8>(v >> (8 * i));
}
inline u16 rd16(const u8* p, bool big) {
    return static_cast<u16>(big ? (p[0] << 8) | p[1] : (p[1] << 8) | p[0]);
}
inline void wr16(u8* p, u16 v, bool big) {
    p[big ? 1 : 0] = static_cast<u8>(v);
    p[big ? 0 : 1] = static_cast<u8>(v >> 8);
}
inline std::size_t pad(std::size_t value, std::size_t to) { return (value + to - 1) / to * to; }

inline u32 crc32(const void* data, std::size_t size) {
    const u8* bytes = static_cast<const u8*>(data);
    u32 crc = ~0u;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// New content grows up from `size`; working data grows down from `top`. A refusal keeps its first reason.
struct Work {
    u8* base = nullptr;
    std::size_t size = 0;
    std::size_t capacity = 0;
    std::size_t top = 0;
    const char* refusal = nullptr;

    Work(u8* data, std::size_t used, std::size_t allocated) : base(data), size(used), capacity(allocated) {
        const auto address = reinterpret_cast<std::uintptr_t>(data);
        top = allocated < used ? used : ((address + allocated) & ~std::uintptr_t{7}) - address;
        if (top < used) top = used;
    }
    bool refuse(const char* why) {
        if (!refusal) refusal = why;
        return false;
    }
    std::size_t room() const { return top - size; }
    // Offset of `bytes` new aligned bytes at the end of the file, zeroed; kNone when they do not fit.
    std::size_t grow(std::size_t bytes, std::size_t align = 4) {
        const std::size_t at = pad(size, align);
        if (at > top || bytes > top - at) {
            refuse("no room left after the file");
            return kNone;
        }
        std::memset(base + size, 0, at + bytes - size);
        size = at + bytes;
        return at;
    }
    // Working bytes at the top of the spare room, 8-aligned and zeroed; released with `release`.
    u8* scratch(std::size_t bytes) {
        bytes = pad(bytes, 8);
        if (bytes > top - size) {
            refuse("no room for working data");
            return nullptr;
        }
        top -= bytes;
        std::memset(base + top, 0, bytes);
        return base + top;
    }
    template <typename T>
    T* scratchArray(std::size_t count) { return reinterpret_cast<T*>(scratch(count * sizeof(T))); }
    void release(std::size_t mark) { top = mark; }
};

// What one merge did, for the log line.
struct Report {
    u32 added = 0;
    u32 present = 0;
    u32 strings = 0;
    u32 skipped = 0;
};

}  // namespace fieldworks::merge
