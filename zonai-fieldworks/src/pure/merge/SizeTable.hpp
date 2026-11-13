// Copyright (c) HanSoloBlaster, afiaka87
// Raises and adds budgets in a loaded resource-size table (RESTBL) in place.
#pragma once

#include "Work.hpp"

namespace fieldworks::merge {

// A budget for one named file that replaces the merge data's value for that file.
struct Budget {
    const char* name;
    u32 size;
};

// The merge data: "FWRT", count, then {name hash, bytes} sorted by hash. Every budget becomes at least
// the given size; a hash the table lacks is inserted in order. No budget is lowered.
inline bool mergeSizeTable(Work& w, const u8* delta, std::size_t deltaSize, const Budget* overrides,
                           u32 overrideCount, Report& report) {
    u8* d = w.base;
    if (w.size < 22 || std::memcmp(d, "RESTBL", 6)) return w.refuse("the loaded file is not a size table");
    const u32 width = rd32(d + 10, false), n = rd32(d + 14, false), named = rd32(d + 18, false);
    const std::size_t namedAt = 22 + 8 * std::size_t{n}, namedBytes = std::size_t{named} * (std::size_t{width} + 4);
    if (width < 8 || width > 1024 || namedAt > w.size || namedBytes != w.size - namedAt)
        return w.refuse("the loaded file is not a size table this build understands");
    if (deltaSize < 8 || std::memcmp(delta, "FWRT", 4)) return w.refuse("the merge data is unreadable");
    const u32 m = rd32(delta + 4, false);
    if (8 + 8 * std::size_t{m} != deltaSize) return w.refuse("the merge data is unreadable");
    const auto hashAt = [&](u32 i) { return rd32(d + 22 + 8 * std::size_t{i}, false); };
    const auto wantHash = [&](u32 j) { return rd32(delta + 8 + 8 * std::size_t{j}, false); };
    // The size wanted for merge-data entry `j`: an override for the same file wins.
    const auto wantSize = [&](u32 j) {
        for (u32 o = 0; o < overrideCount; ++o)
            if (crc32(overrides[o].name, std::strlen(overrides[o].name)) == wantHash(j)) return overrides[o].size;
        return rd32(delta + 12 + 8 * std::size_t{j}, false);
    };
    for (u32 i = 1; i < n; ++i)
        if (hashAt(i) <= hashAt(i - 1)) return w.refuse("the loaded size table is not sorted");
    u32 inserts = 0;
    for (u32 j = 0, i = 0; j < m; ++j) {
        if (j && wantHash(j) <= wantHash(j - 1)) return w.refuse("the merge data is unreadable");
        while (i < n && hashAt(i) < wantHash(j)) ++i;
        if (i == n || hashAt(i) != wantHash(j)) ++inserts;
    }
    const std::size_t grown = 8 * std::size_t{inserts};
    if (grown > w.room()) return w.refuse("no room left after the file");

    // A file the table lists by name is looked up there first, so its budget is raised there too.
    for (u32 e = 0; e < named; ++e) {
        u8* entry = d + namedAt + std::size_t{e} * (std::size_t{width} + 4);
        for (u32 o = 0; o < overrideCount; ++o) {
            const std::size_t length = std::strlen(overrides[o].name);
            if (length < width && !std::memcmp(entry, overrides[o].name, length + 1) &&
                rd32(entry + width, false) < overrides[o].size) {
                wr32(entry + width, overrides[o].size, false);
                ++report.added;
            }
        }
    }
    std::memmove(d + namedAt + grown, d + namedAt, namedBytes);
    // Merge from the top so every existing entry is read before its place is reused.
    for (u32 out = n + inserts, i = n, j = m; out-- > 0;) {
        u8* entry = d + 22 + 8 * std::size_t{out};
        if (j && (!i || wantHash(j - 1) >= hashAt(i - 1))) {
            --j;
            const bool same = i && hashAt(i - 1) == wantHash(j);
            const u32 have = same ? rd32(d + 22 + 8 * std::size_t{i - 1} + 4, false) : 0, want = wantSize(j);
            if (same) --i;
            if (want > have) ++report.added; else ++report.present;
            wr32(entry + 4, want > have ? want : have, false);
            wr32(entry, wantHash(j), false);
        } else {
            --i;
            const u32 hash = hashAt(i), size = rd32(d + 22 + 8 * std::size_t{i} + 4, false);
            wr32(entry, hash, false);
            wr32(entry + 4, size, false);
        }
    }
    wr32(d + 14, n + inserts, false);
    w.size += grown;
    return true;
}

// The budget the table gives `name` (without ".zs"); 0 when it has none.
inline u32 budgetOf(const u8* d, std::size_t size, const char* name) {
    if (size < 22 || std::memcmp(d, "RESTBL", 6)) return 0;
    const u32 width = rd32(d + 10, false), n = rd32(d + 14, false), named = rd32(d + 18, false);
    const std::size_t namedAt = 22 + 8 * std::size_t{n}, length = std::strlen(name);
    if (namedAt > size || std::size_t{named} * (std::size_t{width} + 4) > size - namedAt) return 0;
    for (u32 e = 0; e < named; ++e) {
        const u8* entry = d + namedAt + std::size_t{e} * (std::size_t{width} + 4);
        if (length < width && !std::memcmp(entry, name, length + 1)) return rd32(entry + width, false);
    }
    const u32 hash = crc32(name, length);
    u32 low = 0, high = n;
    while (low < high) {
        const u32 mid = (low + high) / 2;
        if (rd32(d + 22 + 8 * std::size_t{mid}, false) < hash) low = mid + 1; else high = mid;
    }
    return low < n && rd32(d + 22 + 8 * std::size_t{low}, false) == hash ? rd32(d + 26 + 8 * std::size_t{low}, false) : 0;
}

}  // namespace fieldworks::merge
