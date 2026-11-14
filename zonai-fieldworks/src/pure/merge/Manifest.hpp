// Copyright (c) HanSoloBlaster, afiaka87
// The list of game files Fieldworks adds its own content to, as the build writes it. No game code.
#pragma once

#include "Byml.hpp"

namespace fieldworks::merge {

enum class Kind : u8 { Table, Rows, GameData, Tag, Messages, Sounds };

inline const char* kindName(Kind kind) {
    constexpr const char* names[] = {"table", "rows", "gamedata", "tag", "messages", "sounds"};
    return names[static_cast<u8>(kind)];
}

// One shared file: its name without ".zs", the merge data's path, the packed and unpacked sizes of
// Fieldworks' own copy, and the spare room a merge needs (eighths of the unpacked size plus fixed bytes).
struct Entry {
    Kind kind = Kind::Rows;
    const char* name = nullptr;
    const char* delta = nullptr;
    u32 packed = 0, size = 0, eighths = 0, fixed = 0;
};

constexpr u32 kMaxEntries = 32;  // was 16 until every text language got an entry (2026-11-13)

struct Manifest {
    Entry entries[kMaxEntries];
    u32 count = 0;
};

// Reads the entries for one game version ("121", "143"). The names point into `data`.
inline bool readManifest(const u8* data, std::size_t size, const char* version, Manifest& out) {
    Byml b;
    out.count = 0;
    if (!b.open(data, size) || b.d[b.root] != kDict) return false;
    const int at = b.field(b.root, version, kArray);
    if (at < 0) return false;
    const u32 list = b.entryValue(b.root, static_cast<u32>(at));
    if (!b.container(list, kArray) || !b.count(list) || b.count(list) > kMaxEntries) return false;
    const u32 strings = b.tableCount(b.strings);
    for (u32 i = 0; i < b.count(list); ++i) {
        const u32 dict = b.item(list, i);
        if (b.itemType(list, i) != kDict || !b.container(dict, kDict)) return false;
        const auto text = [&](const char* name) -> const char* {
            const int field = b.field(dict, name, kString);
            const u32 index = field < 0 ? kNone : b.entryValue(dict, static_cast<u32>(field));
            return index < strings ? b.text(b.strings, index) : nullptr;
        };
        const auto number = [&](const char* name, u32& value) {
            const int field = b.field(dict, name, kUInt);
            if (field >= 0) value = b.entryValue(dict, static_cast<u32>(field));
            return field >= 0;
        };
        Entry& e = out.entries[i];
        const char* kind = text("Kind");
        e.name = text("Name");
        e.delta = text("Delta");
        if (!kind || !e.name || !e.delta || !*e.name || !number("Packed", e.packed) || !number("Size", e.size) ||
            !number("Eighths", e.eighths) || !number("Fixed", e.fixed))
            return false;
        int found = -1;
        for (int k = 0; k < 6; ++k)
            if (!std::strcmp(kind, kindName(static_cast<Kind>(k)))) found = k;
        if (found < 0) return false;
        e.kind = static_cast<Kind>(found);
    }
    out.count = b.count(list);
    return true;
}

// The bytes the game is asked to allocate for a file that unpacks to `unpacked` bytes.
inline u64 plannedBytes(const Entry& e, u64 unpacked) {
    return pad(static_cast<std::size_t>(unpacked + unpacked * e.eighths / 8 + e.fixed), 32);
}

// True when the game's path names this entry: the name ends the path, with or without ".zs".
inline bool pathNames(const char* path, const char* name) {
    const char* at = std::strstr(path, name);
    if (!at) return false;
    const char* rest = at + std::strlen(name);
    return !*rest || !std::strcmp(rest, ".zs");
}

}  // namespace fieldworks::merge
