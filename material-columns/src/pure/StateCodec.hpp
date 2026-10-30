// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "SelectionState.hpp"
#include <limits>

namespace material_columns::storage {
struct ProfileKey {
    std::uint64_t userLow{}, userHigh{};
    std::uint32_t playData{};
    constexpr bool valid() const { return (userLow || userHigh) && playData && playData <= 0x7fffffffu; }
    constexpr bool operator==(const ProfileKey& other) const {
        return userLow == other.userLow && userHigh == other.userHigh && playData == other.playData;
    }
};
inline constexpr std::size_t headerBytes = 64, entryBytes = 20, checksumBytes = 8;
template<std::size_t Groups> inline constexpr std::size_t maximumBytes = headerBytes + Groups * entryBytes + checksumBytes;

inline void write(std::uint8_t* bytes, std::size_t offset, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
inline std::uint64_t read(const std::uint8_t* bytes, std::size_t offset, unsigned width) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= std::uint64_t(bytes[offset + i]) << (8 * i);
    return value;
}
inline std::uint64_t checksum(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}
inline constexpr std::uint8_t magic[]{'M', 'C', 'O', 'L', 'S', 'T', 'A', 'T'};

template<std::size_t Groups>
std::size_t encode(const Preferences<Groups>& state, ProfileKey key, std::uint64_t sequence,
                   std::uint8_t* bytes, std::size_t capacity) {
    if (!bytes || !key.valid() || !sequence || state.count > Groups || state.stockPattern < 6 || state.stockPattern > 9)
        return 0;
    const auto size = headerBytes + state.count * entryBytes + checksumBytes;
    if (size > capacity) return 0;
    for (std::size_t i = 0; i < state.count; ++i) {
        if (!state.items[i].group || !state.items[i].item) return 0;
        for (std::size_t j = 0; j < i; ++j) if (state.items[i].group == state.items[j].group) return 0;
    }
    for (std::size_t i = 0; i < size; ++i) bytes[i] = 0;
    for (unsigned i = 0; i < sizeof(magic); ++i) bytes[i] = magic[i];
    write(bytes, 8, 1, 2); // format version
    write(bytes, 10, headerBytes, 2);
    write(bytes, 12, state.count, 4);
    write(bytes, 16, key.userLow, 8);
    write(bytes, 24, key.userHigh, 8);
    write(bytes, 32, key.playData, 4);
    bytes[36] = state.columns;
    bytes[37] = state.stockPattern;
    write(bytes, 40, sequence, 8);
    write(bytes, 48, state.selected.id, 8);
    write(bytes, 56, state.selected.rank, 4);
    for (std::size_t i = 0; i < state.count; ++i) {
        const auto at = headerBytes + i * entryBytes;
        write(bytes, at, state.items[i].group, 8);
        write(bytes, at + 8, state.items[i].item, 8);
        write(bytes, at + 16, state.items[i].rank, 4);
    }
    write(bytes, size - checksumBytes, checksum(bytes, size - checksumBytes), checksumBytes);
    return size;
}

template<std::size_t Groups>
bool decode(const std::uint8_t* bytes, std::size_t size, ProfileKey expected,
            Preferences<Groups>& state, std::uint64_t& sequence) {
    if (!bytes || !expected.valid() || size < headerBytes + checksumBytes || size > maximumBytes<Groups>) return false;
    for (unsigned i = 0; i < sizeof(magic); ++i) if (bytes[i] != magic[i]) return false;
    const auto count = read(bytes, 12, 4);
    if (read(bytes, 8, 2) != 1 || read(bytes, 10, 2) != headerBytes || count > Groups ||
        size != headerBytes + count * entryBytes + checksumBytes || bytes[36] > 1 || bytes[37] < 6 || bytes[37] > 9 ||
        read(bytes, 38, 2) || read(bytes, 60, 4) || !read(bytes, 40, 8) ||
        read(bytes, size - checksumBytes, checksumBytes) != checksum(bytes, size - checksumBytes)) return false;
    const ProfileKey key{read(bytes, 16, 8), read(bytes, 24, 8), static_cast<std::uint32_t>(read(bytes, 32, 4))};
    if (!(key == expected)) return false;
    Preferences<Groups> result{};
    result.count = static_cast<std::size_t>(count);
    result.columns = bytes[36] != 0;
    result.stockPattern = bytes[37];
    result.selected = {read(bytes, 48, 8), static_cast<std::uint32_t>(read(bytes, 56, 4))};
    for (std::size_t i = 0; i < count; ++i) {
        const auto at = headerBytes + i * entryBytes;
        result.items[i] = {read(bytes, at, 8), read(bytes, at + 8, 8), static_cast<std::uint32_t>(read(bytes, at + 16, 4))};
        if (!result.items[i].group || !result.items[i].item) return false;
        for (std::size_t j = 0; j < i; ++j) if (result.items[j].group == result.items[i].group) return false;
    }
    state = result;
    sequence = read(bytes, 40, 8);
    return true;
}
// Two independent files: replace the older one and retain the last valid file until
// the replacement's full contents pass read-back. A torn write cannot invalidate both.
inline int newest(bool validA, std::uint64_t a, bool validB, std::uint64_t b) {
    if (!validA) return validB ? 1 : -1;
    return validB && b > a ? 1 : 0;
}
} // namespace material_columns::storage
