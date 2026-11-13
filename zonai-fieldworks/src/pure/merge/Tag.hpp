// Copyright (c) HanSoloBlaster, afiaka87
// Adds actors to a loaded actor tag table (RSDB/Tag): path list, tag bits and the per-tag running counts.
#pragma once

#include "Byml.hpp"

namespace fieldworks::merge {

namespace detail {

struct TagTable {
    const Byml& b;
    u32 paths = 0, tags = 0, bits = 0;
    int pathsAt = -1, bitsAt = -1, rankAt = -1;
    u32 rows = 0, tagCount = 0;

    explicit TagTable(const Byml& table) : b(table) {}

    bool open() {
        if (b.d[b.root] != kDict) return false;
        pathsAt = b.field(b.root, "PathList", kArray);
        bitsAt = b.field(b.root, "BitTable", kBinary);
        rankAt = b.field(b.root, "RankTable", kBinary);
        const int tagsAt = b.field(b.root, "TagList", kArray);
        if (pathsAt < 0 || bitsAt < 0 || rankAt < 0 || tagsAt < 0) return false;
        paths = b.entryValue(b.root, static_cast<u32>(pathsAt));
        tags = b.entryValue(b.root, static_cast<u32>(tagsAt));
        bits = b.entryValue(b.root, static_cast<u32>(bitsAt));
        if (!b.container(paths, kArray) || !b.container(tags, kArray) || b.count(paths) % 3) return false;
        rows = b.count(paths) / 3;
        tagCount = b.count(tags);
        const u32 strings = b.tableCount(b.strings);
        for (u32 i = 0; i < 3 * rows; ++i)
            if (b.itemType(paths, i) != kString || b.item(paths, i) >= strings) return false;
        for (u32 i = 0; i < tagCount; ++i)
            if (b.itemType(tags, i) != kString || b.item(tags, i) >= strings) return false;
        const u64 need = (u64{rows} * tagCount + 7) / 8;
        return b.span(bits, 4) && b.word(bits) == need && b.span(bits, 4 + need);
    }
    const char* part(u32 row, u32 which) const { return b.text(b.strings, b.item(paths, 3 * row + which)); }
    bool bit(u32 row, u32 tag) const {
        const u64 at = u64{row} * tagCount + tag;
        return (b.d[bits + 4 + at / 8] >> (at % 8)) & 1;
    }
    int tagNamed(const char* name) const {
        for (u32 i = 0; i < tagCount; ++i)
            if (!std::strcmp(b.text(b.strings, b.item(tags, i)), name)) return static_cast<int>(i);
        return -1;
    }
};

// Orders an actor of the merge data against a row of the table, part by part.
inline int compareTriple(const Byml& s, u32 actor, const TagTable& table, u32 row) {
    for (u32 which = 0; which < 3; ++which) {
        const int order = std::strcmp(s.text(s.strings, s.item(actor, which)), table.part(row, which));
        if (order) return order;
    }
    return 0;
}

}  // namespace detail

// The merge data is an array of [path, name, suffix, tag...] sorted by the first three. An actor the table
// already lists is left alone; a tag the table does not define is skipped and counted.
inline bool mergeTag(Work& w, const u8* delta, std::size_t deltaSize, Report& report) {
    Byml t, s;
    if (!t.open(w.base, w.size, &w) || t.big) return w.refuse("the loaded file is not a tag table this build understands");
    if (!s.open(delta, deltaSize) || s.big || s.d[s.root] != kArray) return w.refuse("the merge data is unreadable");
    detail::TagTable table(t);
    if (!table.open()) return w.refuse("the loaded file is not a tag table this build understands");
    const u32 n = table.rows, m = s.count(s.root), tags = table.tagCount;
    const std::size_t mark = w.top;
    u32* place = w.scratchArray<u32>(m + 1);
    const char** names = w.scratchArray<const char*>(s.tableCount(s.strings) + 1);
    if (!place || !names) return false;

    bool sorted = true;
    for (u32 i = 1; i < n && sorted; ++i)
        for (u32 which = 0; which < 3; ++which) {
            const int order = std::strcmp(table.part(i - 1, which), table.part(i, which));
            if (order < 0) break;
            if (order > 0 || which == 2) sorted = false;
        }
    u32 adds = 0;
    const u32 strings = s.tableCount(s.strings);
    for (u32 j = 0; j < m; ++j) {
        const u32 actor = s.item(s.root, j);
        if (s.itemType(s.root, j) != kArray || !s.container(actor, kArray) || s.count(actor) < 3)
            return w.refuse("the merge data is unreadable");
        for (u32 i = 0; i < s.count(actor); ++i)
            if (s.itemType(actor, i) != kString || s.item(actor, i) >= strings)
                return w.refuse("the merge data is unreadable");
        u32 at = n;
        bool found = false;
        if (sorted) {
            u32 low = 0, high = n;
            while (low < high) {
                const u32 mid = (low + high) / 2;
                if (detail::compareTriple(s, actor, table, mid) > 0) low = mid + 1; else high = mid;
            }
            at = low;
            found = low < n && detail::compareTriple(s, actor, table, low) == 0;
        } else {
            for (u32 i = 0; i < n && !found; ++i) found = detail::compareTriple(s, actor, table, i) == 0;
        }
        place[j] = found ? kNone : at;
        adds += found ? 0 : 1;
    }
    report.present = m - adds;
    if (!adds) {
        w.release(mark);
        return true;
    }
    const u32 total = n + adds, blocks = (total + 31) / 32;
    const u64 bitBytes = (u64{total} * tags + 7) / 8, rankBytes = u64{tags} * blocks * 4;
    const u32 k = missingStrings(t, s, names);
    if (stringRoom(t, names, k) + arrayBytes(3 * std::size_t{total}) + bitBytes + rankBytes + 64 > w.room())
        return w.refuse("no room left after the file");
    if (!addStrings(t, names, k)) return false;

    const u32 paths = t.newArray(3 * total);
    const std::size_t bits = w.grow(4 + static_cast<std::size_t>(bitBytes));
    const std::size_t rank = w.grow(4 + static_cast<std::size_t>(rankBytes));
    if (paths == kNone || bits == kNone || rank == kNone) return false;
    t.put(bits, static_cast<u32>(bitBytes));
    t.put(rank, static_cast<u32>(rankBytes));
    u8* newBits = w.base + bits + 4;
    u32 out = 0, j = 0, skipped = 0;
    const auto setBit = [&](u32 row, u32 tag) {
        const u64 at = u64{row} * tags + tag;
        newBits[at / 8] = static_cast<u8>(newBits[at / 8] | (1u << (at % 8)));
    };
    const auto emitNew = [&](u32 before) {
        for (; j < m && (place[j] == kNone || place[j] <= before); ++j) {
            if (place[j] == kNone) continue;
            const u32 actor = s.item(s.root, j);
            for (u32 which = 0; which < 3; ++which)
                t.setItem(paths, 3 * out + which, kString,
                          static_cast<u32>(t.tableFind(t.strings, s.text(s.strings, s.item(actor, which)))));
            for (u32 i = 3; i < s.count(actor); ++i) {
                const int tag = table.tagNamed(s.text(s.strings, s.item(actor, i)));
                if (tag < 0) ++skipped;
                else setBit(out, static_cast<u32>(tag));
            }
            ++out;
        }
    };
    for (u32 i = 0; i < n; ++i) {
        emitNew(i);
        for (u32 which = 0; which < 3; ++which) t.setItem(paths, 3 * out + which, kString, t.item(table.paths, 3 * i + which));
        for (u32 tag = 0; tag < tags; ++tag)
            if (table.bit(i, tag)) setBit(out, tag);
        ++out;
    }
    emitNew(n);
    // Per tag, one count per 32 actors: how many actors up to the end of that block carry the tag.
    for (u32 tag = 0; tag < tags; ++tag) {
        u32 running = 0;
        for (u32 block = 0; block < blocks; ++block) {
            for (u32 row = 32 * block; row < 32 * block + 32 && row < total; ++row) {
                const u64 at = u64{row} * tags + tag;
                running += (newBits[at / 8] >> (at % 8)) & 1;
            }
            wr32(w.base + rank + 4 + 4 * (std::size_t{tag} * blocks + block), running, false);
        }
    }
    t.setEntryValue(t.root, static_cast<u32>(table.pathsAt), paths);
    t.setEntryValue(t.root, static_cast<u32>(table.bitsAt), static_cast<u32>(bits));
    t.setEntryValue(t.root, static_cast<u32>(table.rankAt), static_cast<u32>(rank));
    report.added = adds;
    report.strings = k;
    report.skipped = skipped;
    w.release(mark);
    return true;
}

}  // namespace fieldworks::merge
