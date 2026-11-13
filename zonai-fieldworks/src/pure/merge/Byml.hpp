// Copyright (c) HanSoloBlaster, afiaka87
// Reads a BYML document in place and appends new nodes after it. Either byte order; no game code.
#pragma once

#include "Work.hpp"

namespace fieldworks::merge {

enum : u8 {
    kString = 0xa0, kBinary = 0xa1, kArray = 0xc0, kDict = 0xc1, kTable = 0xc2, kBool = 0xd0, kInt = 0xd1,
    kFloat = 0xd2, kUInt = 0xd3, kInt64 = 0xd4, kUInt64 = 0xd5, kDouble = 0xd6, kNull = 0xff,
};

struct Byml {
    Work* work = nullptr;  // set for the loaded file, null for read-only merge data
    const u8* d = nullptr;
    std::size_t limit = 0;
    bool big = false;
    u32 keys = 0, strings = 0, root = 0;

    std::size_t end() const { return work ? work->size : limit; }
    u8* m() const { return work->base; }
    u32 word(std::size_t at) const { return rd32(d + at, big); }
    void put(std::size_t at, u32 value) const { wr32(m() + at, value, big); }
    u32 count(u32 node) const { return big ? word(node) & 0xffffff : word(node) >> 8; }
    void head(u32 node, u8 type, u32 n) const { put(node, big ? (u32{type} << 24) | n : (n << 8) | type); }

    bool container(u32 node, u8 type) const {
        if (node % 4 || node < 16 || node > end() || end() - node < 4 || d[node] != type) return false;
        const std::size_t n = count(node);
        return (type == kArray ? 4 + pad(n, 4) + 4 * n : 4 + 8 * n) <= end() - node;
    }
    bool span(u32 at, std::size_t bytes) const { return at <= end() && bytes <= end() - at; }

    u8 itemType(u32 array, u32 i) const { return d[array + 4 + i]; }
    std::size_t itemsAt(u32 array) const { return array + 4 + pad(count(array), 4); }
    u32 item(u32 array, u32 i) const { return word(itemsAt(array) + 4 * i); }
    void setItem(u32 array, u32 i, u8 type, u32 value) const {
        m()[array + 4 + i] = type;
        put(itemsAt(array) + 4 * i, value);
    }

    u32 entryKey(u32 dict, u32 i) const {
        const u32 v = word(dict + 4 + 8 * i);
        return big ? v >> 8 : v & 0xffffff;
    }
    u8 entryType(u32 dict, u32 i) const { return d[dict + 4 + 8 * i + 3]; }
    u32 entryValue(u32 dict, u32 i) const { return word(dict + 8 + 8 * i); }
    void setEntry(u32 dict, u32 i, u32 key, u8 type, u32 value) const {
        put(dict + 4 + 8 * i, big ? (key << 8) | type : key | (u32{type} << 24));
        put(dict + 8 + 8 * i, value);
    }
    void setEntryValue(u32 dict, u32 i, u32 value) const { put(dict + 8 + 8 * i, value); }
    int find(u32 dict, u32 key) const {
        u32 low = 0, high = count(dict);
        while (low < high) {
            const u32 mid = (low + high) / 2;
            const u32 at = entryKey(dict, mid);
            if (at == key) return static_cast<int>(mid);
            if (at < key) low = mid + 1; else high = mid;
        }
        return -1;
    }
    // Entry of `dict` named `name` holding `type`; -1 when absent or of another type.
    int field(u32 dict, const char* name, u8 type) const {
        const int key = tableFind(keys, name);
        const int at = key < 0 ? -1 : find(dict, static_cast<u32>(key));
        return at >= 0 && entryType(dict, static_cast<u32>(at)) == type ? at : -1;
    }

    // Entry of `dict` named `name` whatever it holds; -1 when absent.
    int named(u32 dict, const char* name) const {
        const int key = tableFind(keys, name);
        return key < 0 ? -1 : find(dict, static_cast<u32>(key));
    }

    u32 tableCount(u32 table) const { return table ? count(table) : 0; }
    const char* text(u32 table, u32 i) const {
        return reinterpret_cast<const char*>(d) + table + word(table + 4 + 4 * i);
    }
    u32 lowerBound(u32 table, const char* name) const {
        u32 low = 0, high = tableCount(table);
        while (low < high) {
            const u32 mid = (low + high) / 2;
            if (std::strcmp(text(table, mid), name) < 0) low = mid + 1; else high = mid;
        }
        return low;
    }
    int tableFind(u32 table, const char* name) const {
        const u32 at = lowerBound(table, name);
        return at < tableCount(table) && std::strcmp(text(table, at), name) == 0 ? static_cast<int>(at) : -1;
    }
    // A table this code accepts: inside the file, strings back to back, terminated, strictly ascending.
    bool checkTable(u32 table) const {
        if (!table) return true;
        if (table % 4 || !span(table, 8) || d[table] != kTable) return false;
        const std::size_t n = count(table);
        if (!span(table, 4 + 4 * (n + 1))) return false;
        u32 at = word(table + 4);
        if (at != 4 + 4 * (n + 1)) return false;
        for (std::size_t i = 0; i < n; ++i) {
            const u32 next = word(table + 8 + 4 * i);
            if (next <= at || !span(table, next) || d[table + next - 1] != 0) return false;
            if (std::strlen(reinterpret_cast<const char*>(d) + table + at) != next - at - 1) return false;
            if (i && std::strcmp(text(table, static_cast<u32>(i - 1)), text(table, static_cast<u32>(i))) >= 0)
                return false;
            at = next;
        }
        return true;
    }

    bool open(const u8* data, std::size_t size, Work* target = nullptr) {
        work = target;
        d = data;
        limit = size;
        if (size < 16) return false;
        if (data[0] == 'B' && data[1] == 'Y') big = true;
        else if (data[0] == 'Y' && data[1] == 'B') big = false;
        else return false;
        const u16 version = rd16(data + 2, big);
        if (version < 4 || version > 7) return false;
        keys = word(4);
        strings = word(8);
        root = word(12);
        if (!span(root, 4) || (d[root] != kArray && d[root] != kDict)) return false;
        return checkTable(keys) && checkTable(strings) && container(root, d[root]);
    }

    u32 newArray(u32 n) const {
        const std::size_t at = work->grow(4 + pad(n, 4) + 4 * std::size_t{n});
        if (at == kNone) return kNone;
        head(static_cast<u32>(at), kArray, n);
        return static_cast<u32>(at);
    }
    u32 newDict(u32 n) const {
        const std::size_t at = work->grow(4 + 8 * std::size_t{n});
        if (at == kNone) return kNone;
        head(static_cast<u32>(at), kDict, n);
        return static_cast<u32>(at);
    }
    // `array` with `extra` more items; the caller fills the new ones.
    u32 grownArray(u32 array, u32 extra) const {
        const u32 n = count(array);
        const u32 at = newArray(n + extra);
        if (at == kNone) return kNone;
        for (u32 i = 0; i < n; ++i) setItem(at, i, itemType(array, i), item(array, i));
        return at;
    }
};

inline std::size_t arrayBytes(std::size_t n) { return 4 + pad(n, 4) + 4 * n + 4; }

namespace detail {

struct Walker {
    const Byml* b;
    u8* seen;
    const u32* places;
    u32 placeCount;
    u32 stringCount;
    bool write;
};

inline u32 shift(const Walker& s, u32 index) {
    u32 low = 0, high = s.placeCount;
    while (low < high) {
        const u32 mid = (low + high) / 2;
        if (s.places[mid] <= index) low = mid + 1; else high = mid;
    }
    return low;
}

// Visits every container once. The first pass only checks; the second renumbers string references.
inline bool walk(const Walker& s, u32 node, u8 type, int depth) {
    const Byml& b = *s.b;
    if (depth > 24 || !b.container(node, type)) return false;
    u8& cell = s.seen[node >> 5];
    const u8 mask = static_cast<u8>(1u << ((node >> 2) & 7));
    if (cell & mask) return true;
    cell |= mask;
    const u32 n = b.count(node);
    for (u32 i = 0; i < n; ++i) {
        const u8 t = type == kArray ? b.itemType(node, i) : b.entryType(node, i);
        const u32 v = type == kArray ? b.item(node, i) : b.entryValue(node, i);
        if (t == kString) {
            if (v >= s.stringCount) return false;
            if (!s.write) continue;
            if (type == kArray) b.put(b.itemsAt(node) + 4 * i, v + shift(s, v));
            else b.setEntryValue(node, i, v + shift(s, v));
        } else if (t == kArray || t == kDict) {
            if (!walk(s, v, t, depth + 1)) return false;
        } else if (t == kBinary || t == kInt64 || t == kUInt64 || t == kDouble) {
            if (!b.span(v, t == kBinary ? 4 : 8)) return false;
        }
    }
    return true;
}

}  // namespace detail

// Bytes `addStrings` needs after the file, working data included.
inline std::size_t stringRoom(const Byml& b, const char* const* names, u32 k) {
    if (!k) return 0;
    const u32 n = b.tableCount(b.strings);
    std::size_t bytes = 4 + 4 * (std::size_t{n} + k + 1) + (b.word(b.strings + 4 + 4 * n) - b.word(b.strings + 4));
    for (u32 j = 0; j < k; ++j) bytes += std::strlen(names[j]) + 1;
    return bytes + 8 * std::size_t{k} + b.end() / 32 + 64;
}

// `names` are sorted, distinct and absent. The table is rewritten sorted after the file and every string
// reference in the document is renumbered; nothing changes when this refuses.
inline bool addStrings(Byml& b, const char* const* names, u32 k) {
    if (!k) return true;
    Work& w = *b.work;
    if (!b.strings) return w.refuse("the file has no string table");
    if (stringRoom(b, names, k) > w.room()) return w.refuse("no room left after the file");
    const std::size_t mark = w.top;
    const u32 n = b.tableCount(b.strings);
    u32* places = w.scratchArray<u32>(k);
    const std::size_t seenBytes = b.end() / 32 + 8;
    u8* seen = w.scratch(seenBytes);
    if (!places || !seen) return false;
    std::size_t bytes = 4 + 4 * (std::size_t{n} + k + 1) + (b.word(b.strings + 4 + 4 * n) - b.word(b.strings + 4));
    for (u32 j = 0; j < k; ++j) {
        places[j] = b.lowerBound(b.strings, names[j]);
        bytes += std::strlen(names[j]) + 1;
    }
    detail::Walker walker{&b, seen, places, k, n, false};
    if (!detail::walk(walker, b.root, b.d[b.root], 0)) {
        w.release(mark);
        return w.refuse("the file's layout is not understood");
    }
    const std::size_t at = w.grow(bytes);
    if (at == kNone) {
        w.release(mark);
        return false;
    }
    b.head(static_cast<u32>(at), kTable, n + k);
    u32 cursor = 4 + 4 * (n + k + 1), out = 0;
    for (u32 i = 0, j = 0; i < n || j < k;) {
        const bool fresh = j < k && places[j] <= i;
        const char* name = fresh ? names[j] : b.text(b.strings, i);
        const u32 length = static_cast<u32>(std::strlen(name)) + 1;
        b.put(at + 4 + 4 * out++, cursor);
        std::memcpy(w.base + at + cursor, name, length);
        cursor += length;
        if (fresh) ++j; else ++i;
    }
    b.put(at + 4 + 4 * out, cursor);
    std::memset(seen, 0, seenBytes);
    walker.write = true;
    detail::walk(walker, b.root, b.d[b.root], 0);
    b.strings = static_cast<u32>(at);
    b.put(8, b.strings);
    w.release(mark);
    return true;
}

// Copies values from merge data into the loaded file. `memo` (one u32 per four source bytes, zeroed) keeps
// a shared source node single. With `dry` nothing is written: the pass only checks keys and shapes.
struct Copier {
    Byml* to;
    const Byml* from;
    u32* memo;
    bool dry;
};

inline bool copyValue(Copier& c, u8 type, u32 value, u32& out, int depth = 0) {
    Byml& to = *c.to;
    const Byml& from = *c.from;
    Work& w = *to.work;
    switch (type) {
    case kBool: case kInt: case kFloat: case kUInt: case kNull:
        out = value;
        return true;
    case kString: {
        if (value >= from.tableCount(from.strings)) return w.refuse("the merge data is damaged");
        if (c.dry) return true;
        const int at = to.tableFind(to.strings, from.text(from.strings, value));
        if (at < 0) return w.refuse("a needed string is missing from the file");
        out = static_cast<u32>(at);
        return true;
    }
    case kBinary: case kInt64: case kUInt64: case kDouble: {
        if (value % 4 || !from.span(value, 8)) return w.refuse("the merge data is damaged");
        const std::size_t bytes = type == kBinary ? 4 + std::size_t{from.word(value)} : 8;
        if (!from.span(value, bytes)) return w.refuse("the merge data is damaged");
        if (c.dry) return true;
        if (c.memo[value / 4]) {
            out = c.memo[value / 4];
            return true;
        }
        const std::size_t at = w.grow(bytes, type == kBinary ? 4 : 8);
        if (at == kNone) return false;
        std::memcpy(w.base + at, from.d + value, bytes);
        out = c.memo[value / 4] = static_cast<u32>(at);
        return true;
    }
    case kArray: case kDict: {
        if (depth > 24 || !from.container(value, type)) return w.refuse("the merge data is damaged");
        if (!c.dry && c.memo[value / 4]) {
            out = c.memo[value / 4];
            return true;
        }
        const u32 n = from.count(value);
        u32 at = 0;
        if (!c.dry) {
            at = type == kArray ? to.newArray(n) : to.newDict(n);
            if (at == kNone) return false;
            c.memo[value / 4] = at;
        }
        int lastKey = -1;
        for (u32 i = 0; i < n; ++i) {
            const u8 t = type == kArray ? from.itemType(value, i) : from.entryType(value, i);
            const u32 v = type == kArray ? from.item(value, i) : from.entryValue(value, i);
            u32 made = 0;
            if (!copyValue(c, t, v, made, depth + 1)) return false;
            if (type == kArray) {
                if (!c.dry) to.setItem(at, i, t, made);
                continue;
            }
            const int key = to.tableFind(to.keys, from.text(from.keys, from.entryKey(value, i)));
            if (key <= lastKey) return w.refuse("the loaded file lacks a key the new content needs");
            lastKey = key;
            if (!c.dry) to.setEntry(at, i, static_cast<u32>(key), t, made);
        }
        out = at;
        return true;
    }
    default:
        return w.refuse("the merge data holds a value type this build does not copy");
    }
}

// Sorted names from the merge data's string table that the loaded file lacks.
inline u32 missingStrings(const Byml& to, const Byml& from, const char** names) {
    u32 k = 0;
    const u32 n = from.tableCount(from.strings);
    for (u32 i = 0; i < n; ++i) {
        const char* name = from.text(from.strings, i);
        if (to.tableFind(to.strings, name) < 0) names[k++] = name;
    }
    return k;
}

}  // namespace fieldworks::merge
