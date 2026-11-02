// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
#include <cstddef>
#include "Native142.hpp"
#include "Native120.hpp"
#include "Native143.hpp"
namespace material_columns::native {
enum class NativeVersion { Unknown, V120, V121, V142, V143 };
#if defined(MATERIAL_COLUMNS_TARGET_143)
inline NativeVersion nativeVersion = NativeVersion::V143;
#elif defined(MATERIAL_COLUMNS_TARGET_142)
inline NativeVersion nativeVersion = NativeVersion::V142;
#else
inline NativeVersion nativeVersion = NativeVersion::V121;
#endif
inline bool is120() { return nativeVersion == NativeVersion::V120; }
inline bool is142() { return nativeVersion == NativeVersion::V142; }
inline bool is143() { return nativeVersion == NativeVersion::V143; }
inline bool is14() { return is142() || is143(); }
inline unsigned nativeFrameOffset() { return is14() ? 16 : 8; }
inline std::uintptr_t nativeDataBegin() { return is143() ? native143DataBegin : native142DataBegin; }
inline std::uintptr_t nativeDataEnd() { return is143() ? native143DataEnd : native142DataEnd; }
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
    bool v120 = totalSize >= 0x47195a8;
    for (const auto& entry : native120Entries)
        if (entry.source == 0x123a220 || entry.source == 0x2a66f00)
            v120 = v120 && matches(entry.target, entry.words[0], entry.words[1]);
    bool v143 = totalSize >= 0x3add328;
    for (const auto& entry : native143Entries)
        if (entry.source == 0x123a220 || entry.source == 0x2a66f00)
            v143 = v143 && matches(entry.target, entry.words[0], entry.words[1]);
    if (unsigned(v120) + unsigned(v121) + unsigned(v142) + unsigned(v143) != 1) return false;
    nativeVersion = v143 ? NativeVersion::V143 : v120 ? NativeVersion::V120 : v142 ? NativeVersion::V142 : NativeVersion::V121;
    return true;
}
inline const NativeEntry* nativeEntriesBegin() {
    return is143() ? native143Entries : is120() ? native120Entries : is142() ? native142Entries : nullptr;
}
inline std::size_t nativeEntriesCount() {
    return is143() ? sizeof(native143Entries)/sizeof(NativeEntry) : is120() ? sizeof(native120Entries)/sizeof(NativeEntry) :
           is142() ? sizeof(native142Entries)/sizeof(NativeEntry) : 0;
}
inline const NativeEntry* nativeEntry(std::uintptr_t offset) {
    for (std::size_t i = 0; i < nativeEntriesCount(); ++i)
        if (nativeEntriesBegin()[i].source == offset) return nativeEntriesBegin() + i;
    return nullptr;
}
inline std::uintptr_t nativeOffset(std::uintptr_t offset) {
    if (nativeVersion == NativeVersion::V121) return offset;
    if (const auto* entry = nativeEntry(offset)) return entry->target;
    return 0;
}
template<class Site> inline Site nativeSite(Site site) {
    if (nativeVersion == NativeVersion::V121) return site;
    if (const auto* entry = nativeEntry(site.offset))
        return {entry->target, entry->words[0], entry->words[1]};
    return {};
}
inline std::uint32_t nativeWord(std::uintptr_t offset, unsigned index, std::uint32_t original) {
    if (nativeVersion == NativeVersion::V121) return original;
    if (index < 5) if (const auto* entry = nativeEntry(offset)) return entry->words[index];
    return 0;
}
inline std::uintptr_t nativeSingleton(std::uintptr_t base, std::uintptr_t source) {
    if (!base || nativeVersion == NativeVersion::Unknown) return 0;
    auto read = [](std::uintptr_t address) { return *reinterpret_cast<const std::uintptr_t*>(address); };
    if (is120()) {
        if (source == 0x4728620) return read(base + 0x47195a0);
        if (source == 0x462e3d8) source = 0x461f3e8;
        else if (source == 0x462df08) source = 0x461ef18;
        else if (source == 0x4630020) source = 0x4621028;
        else return 0;
    } else if (is14()) {
        if (source == 0x462e3d8) return read(base + (is143() ? 0x3ad66b0 : 0x3ac46b0));
        if (source == 0x462df08) return read(base + (is143() ? 0x3ada848 : 0x3ac8840));
        if (source == 0x4728620) return read(base + (is143() ? 0x3add320 : 0x3acb318));
        if (source != 0x4630020) return 0;
        source = is143() ? 0x39601e8 : 0x394e1e8;
    } else if (source == 0x4728620) return read(base + source);
    const auto slot = read(base + source);
    return slot ? read(slot) : 0;
}
}
