// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
#include <span>

namespace botw_abilities::effects {
constexpr std::uint32_t fingerprint(std::span<const std::uint8_t> bytes) {
    std::uint32_t value = 2166136261u;
    for (const auto byte : bytes) value = (value ^ byte) * 16777619u;
    return value;
}

constexpr bool validResource(std::span<const std::uint8_t> bytes, std::uint32_t checksum) {
    if (bytes.size() < 12) return false;
    const auto word = [&bytes](unsigned offset) {
        return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
               (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
    };
    return word(0) == 0x4b4e4c58u && word(4) == bytes.size() && word(8) == 0x24 &&
           fingerprint(bytes) == checksum;
}

constexpr bool hasResourceSlot(int loaded, int count, int capacity) {
    return loaded > 0 && loaded == count && count < capacity && capacity == 8;
}
}
