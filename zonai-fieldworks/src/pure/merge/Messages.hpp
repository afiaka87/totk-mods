// Copyright (c) HanSoloBlaster, afiaka87
// Adds message labels to files inside a loaded message archive (Mals/*.sarc), as the offline tool does.
#pragma once

#include "Byml.hpp"

namespace fieldworks::merge {

namespace detail {

// A little-endian UTF-16 message file with exactly a label section and a text section.
struct MessageFile {
    const u8* d = nullptr;
    std::size_t size = 0;
    const u8* labels = nullptr;
    const u8* text = nullptr;
    u32 labelBytes = 0, textBytes = 0, groups = 0, count = 0;

    bool open(const u8* data, std::size_t bytes) {
        d = data;
        size = bytes;
        if (bytes < 64 || std::memcmp(data, "MsgStdBn\xff\xfe", 10) || data[12] != 1 || rd16(data + 14, false) != 2)
            return false;
        std::size_t at = 32;
        const char* wanted[2] = {"LBL1", "TXT2"};
        for (int i = 0; i < 2; ++i) {
            if (at + 16 > bytes || std::memcmp(data + at, wanted[i], 4)) return false;
            const u32 length = rd32(data + at + 4, false);
            if (length > bytes - at - 16) return false;
            (i ? text : labels) = data + at + 16;
            (i ? textBytes : labelBytes) = length;
            at += 16 + pad(length, 16);
        }
        if (labelBytes < 4 || textBytes < 4) return false;
        groups = rd32(labels, false);
        count = rd32(text, false);
        if (!groups || groups > (labelBytes - 4) / 8 || count > (textBytes - 4) / 4) return false;
        // Strings back to back from the end of the offsets, so the block can move as one piece.
        if (count && rd32(text + 4, false) != 4 + 4 * count) return false;
        for (u32 g = 0; g < groups; ++g) {
            u32 cursor = rd32(labels + 8 + 8 * g, false);
            for (u32 i = 0, n = rd32(labels + 4 + 8 * g, false); i < n; ++i) {
                if (cursor >= labelBytes || labelBytes - cursor < 5u + labels[cursor]) return false;
                cursor += 5u + labels[cursor];
            }
        }
        return true;
    }
    static u32 hash(const char* name) {
        u32 value = 0;
        for (const char* c = name; *c; ++c) value = value * 0x492 + static_cast<u8>(*c);
        return value;
    }
    u32 groupOf(const char* name) const { return hash(name) % groups; }
    u32 groupBytes(u32 g) const {
        u32 cursor = rd32(labels + 8 + 8 * g, false);
        const u32 start = cursor;
        for (u32 i = 0, n = rd32(labels + 4 + 8 * g, false); i < n; ++i) cursor += 5u + labels[cursor];
        return cursor - start;
    }
    bool has(const char* name) const {
        const u32 g = groupOf(name), length = static_cast<u32>(std::strlen(name));
        u32 cursor = rd32(labels + 8 + 8 * g, false);
        for (u32 i = 0, n = rd32(labels + 4 + 8 * g, false); i < n; ++i) {
            if (labels[cursor] == length && !std::memcmp(labels + cursor + 1, name, length)) return true;
            cursor += 5u + labels[cursor];
        }
        return false;
    }
};

struct MessageAdds {
    const Byml* s;
    u32 labels, texts;  // arrays in the merge data: label names (sorted) and their text bytes
    u32 total() const { return s->count(labels); }
    const char* name(u32 i) const { return s->text(s->strings, s->item(labels, i)); }
    u32 textBytes(u32 i) const { return s->word(s->item(texts, i)); }
    const u8* textAt(u32 i) const { return s->d + s->item(texts, i) + 4; }
};

// Writes `old` with the absent labels appended to `out` (null: only measure). Returns the new size.
inline std::size_t appendMessages(const MessageFile& old, const MessageAdds& adds, u8* out, u32& added) {
    u32 labelBytes = old.labelBytes, textBytes = old.textBytes;
    added = 0;
    for (u32 i = 0; i < adds.total(); ++i) {
        if (old.has(adds.name(i))) continue;
        ++added;
        labelBytes += 5u + static_cast<u32>(std::strlen(adds.name(i)));
        textBytes += 4 + adds.textBytes(i);
    }
    const std::size_t textSection = 32 + 16 + pad(labelBytes, 16);
    const std::size_t total = textSection + 16 + pad(textBytes, 16);
    if (!out || !added) return total;

    std::memcpy(out, old.d, 32);
    wr32(out + 18, static_cast<u32>(total), false);
    u8* section = out + 32;
    std::memcpy(section, "LBL1", 4);
    wr32(section + 4, labelBytes, false);
    std::memset(section + 8, 0, 8);
    u8* labels = section + 16;
    wr32(labels, old.groups, false);
    u32 cursor = 4 + 8 * old.groups;
    for (u32 g = 0; g < old.groups; ++g) {
        const u32 start = cursor, bytes = old.groupBytes(g);
        u32 n = rd32(old.labels + 4 + 8 * g, false);
        std::memcpy(labels + cursor, old.labels + rd32(old.labels + 8 + 8 * g, false), bytes);
        cursor += bytes;
        for (u32 i = 0, index = old.count; i < adds.total(); ++i) {
            const char* name = adds.name(i);
            if (old.has(name)) continue;
            if (old.groupOf(name) == g) {
                const u32 length = static_cast<u32>(std::strlen(name));
                labels[cursor] = static_cast<u8>(length);
                std::memcpy(labels + cursor + 1, name, length);
                wr32(labels + cursor + 1 + length, index, false);
                cursor += 5 + length;
                ++n;
            }
            ++index;
        }
        wr32(labels + 4 + 8 * g, n, false);
        wr32(labels + 8 + 8 * g, start, false);
    }
    std::memset(labels + labelBytes, 0xab, pad(labelBytes, 16) - labelBytes);

    section = out + textSection;
    std::memcpy(section, "TXT2", 4);
    wr32(section + 4, textBytes, false);
    std::memset(section + 8, 0, 8);
    u8* text = section + 16;
    const u32 count = old.count + added, moved = 4 * added;
    wr32(text, count, false);
    for (u32 i = 0; i < old.count; ++i) wr32(text + 4 + 4 * i, rd32(old.text + 4 + 4 * i, false) + moved, false);
    const u32 oldStrings = old.textBytes - (4 + 4 * old.count);
    std::memcpy(text + 4 + 4 * count, old.text + 4 + 4 * old.count, oldStrings);
    u32 at = 4 + 4 * count + oldStrings;
    for (u32 i = 0, index = old.count; i < adds.total(); ++i) {
        if (old.has(adds.name(i))) continue;
        wr32(text + 4 + 4 * index++, at, false);
        std::memcpy(text + at, adds.textAt(i), adds.textBytes(i));
        at += adds.textBytes(i);
    }
    std::memset(text + textBytes, 0xab, pad(textBytes, 16) - textBytes);
    return total;
}

struct Archive {
    Work& w;
    u32 data = 0, nodes = 0, multiplier = 0;
    const u8* names = nullptr;
    std::size_t nameBytes = 0;

    explicit Archive(Work& work) : w(work) {}

    bool open() {
        const u8* d = w.base;
        if (w.size < 0x28 || std::memcmp(d, "SARC", 4) || rd16(d + 4, false) != 0x14 || rd16(d + 6, false) != 0xfeff ||
            std::memcmp(d + 0x14, "SFAT", 4) || rd16(d + 0x18, false) != 0xc)
            return false;
        data = rd32(d + 12, false);
        nodes = rd16(d + 0x1a, false);
        multiplier = rd32(d + 0x1c, false);
        const std::size_t table = 0x20 + 16 * std::size_t{nodes};
        if (rd32(d + 8, false) != w.size || data > w.size || table + 8 > data || std::memcmp(d + table, "SFNT", 4))
            return false;
        names = d + table + 8;
        nameBytes = data - table - 8;
        for (u32 i = 0; i < nodes; ++i) {
            const u8* node = d + 0x20 + 16 * i;
            const u32 begin = rd32(node + 8, false), end = rd32(node + 12, false);
            if (begin > end || end > w.size - data) return false;
        }
        return true;
    }
    // Offset of the file table entry for `name`; 0 when the archive has no such file.
    std::size_t find(const char* name) const {
        u32 hash = 0;
        for (const char* c = name; *c; ++c) hash = hash * multiplier + static_cast<u32>(static_cast<signed char>(*c));
        for (u32 i = 0; i < nodes; ++i) {
            const std::size_t node = 0x20 + 16 * std::size_t{i};
            const u32 attributes = rd32(w.base + node + 4, false);
            const std::size_t at = 4 * std::size_t{attributes & 0xffff};
            if (rd32(w.base + node, false) != hash) continue;
            if (!(attributes & 0x01000000)) return node;
            const std::size_t length = std::strlen(name);
            if (at + length < nameBytes && !std::memcmp(names + at, name, length + 1)) return node;
        }
        return 0;
    }
};

}  // namespace detail

// The merge data is an array of {File, Labels (sorted names), Texts (their bytes)}. Each named file is
// rebuilt after the archive with the absent labels appended. A file of another shape is skipped and counted.
inline bool mergeMessages(Work& w, const u8* delta, std::size_t deltaSize, Report& report) {
    Byml s;
    detail::Archive archive(w);
    if (!archive.open()) return w.refuse("the loaded file is not a message archive this build understands");
    if (!s.open(delta, deltaSize) || s.d[s.root] != kArray) return w.refuse("the merge data is unreadable");
    const u32 files = s.count(s.root), strings = s.tableCount(s.strings);
    std::size_t need = 0;
    // First pass measures and checks; the second writes. Nothing changes before the room is known.
    for (int pass = 0; pass < 2; ++pass) {
        if (pass && (!report.added || need > w.room())) return report.added ? w.refuse("no room left after the file") : true;
        if (pass) report.added = report.present = report.skipped = 0;
        for (u32 f = 0; f < files; ++f) {
            const u32 entry = s.item(s.root, f);
            if (s.itemType(s.root, f) != kDict || !s.container(entry, kDict)) return w.refuse("the merge data is unreadable");
            const int fileAt = s.field(entry, "File", kString), labelsAt = s.field(entry, "Labels", kArray);
            const int textsAt = s.field(entry, "Texts", kArray);
            if (fileAt < 0 || labelsAt < 0 || textsAt < 0 || s.entryValue(entry, static_cast<u32>(fileAt)) >= strings)
                return w.refuse("the merge data is unreadable");
            const detail::MessageAdds adds{&s, s.entryValue(entry, static_cast<u32>(labelsAt)),
                                           s.entryValue(entry, static_cast<u32>(textsAt))};
            if (!s.container(adds.labels, kArray) || !s.container(adds.texts, kArray) ||
                s.count(adds.labels) != s.count(adds.texts))
                return w.refuse("the merge data is unreadable");
            for (u32 i = 0; i < adds.total(); ++i) {
                const u32 text = s.item(adds.texts, i);
                if (s.itemType(adds.labels, i) != kString || s.item(adds.labels, i) >= strings ||
                    std::strlen(adds.name(i)) > 255 || s.itemType(adds.texts, i) != kBinary || !s.span(text, 4) ||
                    !s.span(text, 4 + std::size_t{s.word(text)}))
                    return w.refuse("the merge data is unreadable");
            }
            const std::size_t node = archive.find(s.text(s.strings, s.entryValue(entry, static_cast<u32>(fileAt))));
            detail::MessageFile old;
            if (!node || !old.open(w.base + archive.data + rd32(w.base + node + 8, false),
                                   rd32(w.base + node + 12, false) - rd32(w.base + node + 8, false))) {
                report.skipped += adds.total();
                continue;
            }
            u32 added = 0;
            const std::size_t bytes = detail::appendMessages(old, adds, nullptr, added);
            report.added += added;
            report.present += adds.total() - added;
            if (!added) continue;
            if (!pass) {
                need += bytes + 16;
                continue;
            }
            const std::size_t at = w.grow(bytes, 16);
            if (at == kNone) return false;
            detail::appendMessages(old, adds, w.base + at, added);
            wr32(w.base + node + 8, static_cast<u32>(at - archive.data), false);
            wr32(w.base + node + 12, static_cast<u32>(at - archive.data + bytes), false);
            wr32(w.base + 8, static_cast<u32>(w.size), false);
        }
    }
    return true;
}

}  // namespace fieldworks::merge
