// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
#include <cstddef>
#include "Native142.hpp"
namespace material_columns::native {
enum class NativeVersion { Unknown, V121, V142 };
#ifdef MATERIAL_COLUMNS_TARGET_142
inline NativeVersion nativeVersion = NativeVersion::V142;
#else
inline NativeVersion nativeVersion = NativeVersion::V121;
#endif
inline bool is142() { return nativeVersion == NativeVersion::V142; }
inline unsigned nativeFrameOffset() { return is142() ? 16 : 8; }
inline bool selectNativeVersion(std::uintptr_t base, std::size_t textSize, std::size_t totalSize) {
    nativeVersion = NativeVersion::Unknown;
    if (!base || textSize < 0x2a68c40 || totalSize < textSize) return false;
    auto matches = [&](std::uintptr_t offset, std::uint32_t first, std::uint32_t second) {
        if (offset > textSize || textSize - offset < 8) return false;
        const auto* words = reinterpret_cast<const std::uint32_t*>(base + offset);
        return words[0] == first && words[1] == second;
    };
    const bool v121 = totalSize >= 0x4728628 &&
        matches(0x123a220, 0xa9bb7bfd, 0xf9000bf9) && matches(0x2a66f00, 0xa9bd7bfd, 0xf9000bf5);
    bool v142 = totalSize >= 0x3acb320;
    for (const auto& entry : native142Entries)
        if (entry.source == 0x123a220 || entry.source == 0x2a66f00)
            v142 = v142 && matches(entry.target, entry.words[0], entry.words[1]);
    if (v121 == v142) return false;
    nativeVersion = v142 ? NativeVersion::V142 : NativeVersion::V121;
    return true;
}
inline std::uintptr_t nativeOffset(std::uintptr_t offset) {
    if (nativeVersion == NativeVersion::V121) return offset;
    if (is142()) for (const auto& entry : native142Entries) if (entry.source == offset) return entry.target;
    return 0;
}
template<class Site> inline Site nativeSite(Site site) {
    if (nativeVersion == NativeVersion::V121) return site;
    if (is142()) for (const auto& entry : native142Entries) if (entry.source == site.offset)
        return {entry.target, entry.words[0], entry.words[1]};
    return {};
}
inline std::uint32_t nativeWord(std::uintptr_t offset, unsigned index, std::uint32_t original) {
    if (nativeVersion == NativeVersion::V121) return original;
    if (is142() && index < 5) for (const auto& entry : native142Entries)
        if (entry.source == offset) return entry.words[index];
    return 0;
}
inline std::uintptr_t nativeSingleton(std::uintptr_t base, std::uintptr_t source) {
    if (!base || nativeVersion == NativeVersion::Unknown) return 0;
    auto read = [](std::uintptr_t address) { return *reinterpret_cast<const std::uintptr_t*>(address); };
    if (is142()) {
        if (source == 0x462e3d8) return read(base + 0x3ac46b0);
        if (source == 0x462df08) return read(base + 0x3ac8840);
        if (source == 0x4728620) return read(base + 0x3acb318);
        if (source != 0x4630020) return 0;
        source = 0x394e1e8;
    } else if (source == 0x4728620) return read(base + source);
    const auto slot = read(base + source);
    return slot ? read(slot) : 0;
}
}
