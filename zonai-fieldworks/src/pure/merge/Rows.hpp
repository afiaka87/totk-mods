// Copyright (c) HanSoloBlaster, afiaka87
// Adds rows to a loaded actor row table (RSDB/*.rstbl.byml): an array of rows named by "__RowId".
#pragma once

#include "Byml.hpp"

namespace fieldworks::merge {

namespace detail {
inline const char* rowId(const Byml& b, u32 row, u32 key) {
    const int at = b.find(row, key);
    if (at < 0 || b.entryType(row, static_cast<u32>(at)) != kString) return nullptr;
    const u32 index = b.entryValue(row, static_cast<u32>(at));
    return index < b.tableCount(b.strings) ? b.text(b.strings, index) : nullptr;
}
}  // namespace detail

// The merge data is an array of whole rows sorted by row id. A row whose id the file already has is
// left alone; the others go in at their sorted place (at the end when the file is not sorted).
inline bool mergeRows(Work& w, const u8* delta, std::size_t deltaSize, Report& report) {
    Byml t, s;
    if (!t.open(w.base, w.size, &w) || t.d[t.root] != kArray)
        return w.refuse("the loaded file is not a row table this build understands");
    if (!s.open(delta, deltaSize) || s.big != t.big || s.d[s.root] != kArray)
        return w.refuse("the merge data is unreadable");
    const int tKey = t.tableFind(t.keys, "__RowId"), sKey = s.tableFind(s.keys, "__RowId");
    if (tKey < 0 || sKey < 0) return w.refuse("the row table has no row ids");
    const u32 n = t.count(t.root), m = s.count(s.root);
    const std::size_t mark = w.top;
    u32* place = w.scratchArray<u32>(m + 1);
    u32* made = w.scratchArray<u32>(m + 1);
    u32* memo = w.scratchArray<u32>(deltaSize / 4 + 1);
    const char** names = w.scratchArray<const char*>(s.tableCount(s.strings) + 1);
    if (!place || !made || !memo || !names) return false;

    bool sorted = true;
    const char* last = nullptr;
    for (u32 i = 0; i < n; ++i) {
        const u32 row = t.item(t.root, i);
        const char* id = t.itemType(t.root, i) == kDict && t.container(row, kDict)
                             ? detail::rowId(t, row, static_cast<u32>(tKey)) : nullptr;
        if (!id) return w.refuse("the loaded file is not a row table this build understands");
        if (last && std::strcmp(last, id) >= 0) sorted = false;
        last = id;
    }
    u32 adds = 0;
    last = nullptr;
    Copier copier{&t, &s, memo, true};
    for (u32 j = 0; j < m; ++j) {
        const u32 row = s.item(s.root, j);
        const char* id = s.itemType(s.root, j) == kDict && s.container(row, kDict)
                             ? detail::rowId(s, row, static_cast<u32>(sKey)) : nullptr;
        if (!id || (last && std::strcmp(last, id) >= 0)) return w.refuse("the merge data is unreadable");
        last = id;
        u32 at = n;
        bool found = false;
        if (sorted) {
            u32 low = 0, high = n;
            while (low < high) {
                const u32 mid = (low + high) / 2;
                if (std::strcmp(detail::rowId(t, t.item(t.root, mid), static_cast<u32>(tKey)), id) < 0) low = mid + 1;
                else high = mid;
            }
            at = low;
            found = low < n && std::strcmp(detail::rowId(t, t.item(t.root, low), static_cast<u32>(tKey)), id) == 0;
        } else {
            for (u32 i = 0; i < n && !found; ++i)
                found = std::strcmp(detail::rowId(t, t.item(t.root, i), static_cast<u32>(tKey)), id) == 0;
        }
        place[j] = found ? kNone : at;
        if (found) continue;
        ++adds;
        u32 unused = 0;
        if (!copyValue(copier, kDict, row, unused)) return false;
    }
    report.present = m - adds;
    if (!adds) {
        w.release(mark);
        return true;
    }
    const u32 k = missingStrings(t, s, names);
    if (stringRoom(t, names, k) + deltaSize + arrayBytes(std::size_t{n} + adds) + 64 > w.room())
        return w.refuse("no room left after the file");

    if (!addStrings(t, names, k)) return false;
    copier.dry = false;
    for (u32 j = 0; j < m; ++j) {
        if (place[j] == kNone) continue;
        if (!copyValue(copier, kDict, s.item(s.root, j), made[j])) return false;
    }
    const u32 root = t.newArray(n + adds);
    if (root == kNone) return false;
    u32 out = 0, j = 0;
    const auto emitNew = [&](u32 before) {
        for (; j < m && (place[j] == kNone || place[j] <= before); ++j)
            if (place[j] != kNone) t.setItem(root, out++, kDict, made[j]);
    };
    for (u32 i = 0; i < n; ++i) {
        emitNew(i);
        t.setItem(root, out++, kDict, t.item(t.root, i));
    }
    emitNew(n);
    t.root = root;
    t.put(12, root);
    report.added = adds;
    report.strings = k;
    w.release(mark);
    return true;
}

}  // namespace fieldworks::merge
