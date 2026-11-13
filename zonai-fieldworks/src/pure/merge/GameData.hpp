// Copyright (c) HanSoloBlaster, afiaka87
// Adds save flags to a loaded GameDataList and grows its save sizes by the recipe the offline tool uses.
#pragma once

#include "Byml.hpp"

namespace fieldworks::merge {

// The merge data is a dictionary of steps keyed by flag kind: Rows (with Cost), Choices, Entries, Raise
// and Lengths. scripts/merge_deltas.py writes them; each is applied only where the file lacks it.
namespace detail {

struct SaveGrowth {
    u32 offset = 0, size = 0;
    u32 filePos[16] = {}, fileSize[16] = {};
};

struct GameDataPass {
    Byml& t;
    const Byml& s;
    Copier& copier;
    Report& report;
    SaveGrowth growth;
    std::size_t need = 0;
    u32 data = 0;
    bool dry = true;

    GameDataPass(Byml& target, const Byml& source, Copier& values, Report& result)
        : t(target), s(source), copier(values), report(result) {}

    Work& w() const { return *t.work; }
    bool bad() const { return w().refuse("the loaded save-flag list is not laid out as this build expects"); }

    u32 rowWithHash(u32 array, u32 hash, u32 hashKey) const {
        const u32 n = t.count(array);
        for (u32 i = 0; i < n; ++i) {
            const u32 row = t.item(array, i);
            const int at = t.find(row, hashKey);
            if (at >= 0 && t.entryValue(row, static_cast<u32>(at)) == hash) return row;
        }
        return 0;
    }
    // The file's array of flags for the kind named by entry `e` of merge-data dictionary `step`.
    bool kindArray(u32 step, u32 e, int& slot, u32& array) const {
        const int key = t.tableFind(t.keys, s.text(s.keys, s.entryKey(step, e)));
        slot = key < 0 ? -1 : t.find(data, static_cast<u32>(key));
        if (slot < 0 || t.entryType(data, static_cast<u32>(slot)) != kArray) return false;
        array = t.entryValue(data, static_cast<u32>(slot));
        if (!t.container(array, kArray)) return false;
        for (u32 i = 0, n = t.count(array); i < n; ++i)
            if (t.itemType(array, i) != kDict || !t.container(t.item(array, i), kDict)) return false;
        return true;
    }
    static bool hashOf(const Byml& b, u32 row, u32& hash) {
        const int at = b.field(row, "Hash", kUInt);
        if (at < 0) return false;
        hash = b.entryValue(row, static_cast<u32>(at));
        return true;
    }
    int saveFile(u32 row) const {
        const int at = t.field(row, "SaveFileIndex", kInt);
        return at < 0 ? -1 : static_cast<int>(t.entryValue(row, static_cast<u32>(at)));
    }
    // The file's flag that merge-data item `item` edits, and the item's own offset.
    bool edited(u32 list, u32 i, u32 array, u32 hashKey, u32& item, u32& row) const {
        item = s.item(list, i);
        u32 hash = 0;
        if (s.itemType(list, i) != kDict || !s.container(item, kDict) || !hashOf(s, item, hash))
            return w().refuse("the merge data is unreadable");
        row = rowWithHash(array, hash, hashKey);
        return row || w().refuse("a save flag the new content edits is missing from the loaded list");
    }

    bool rows(u32 step, u32 costs) {
        for (u32 e = 0, kinds = s.count(step); e < kinds; ++e) {
            int slot = 0;
            u32 array = 0;
            if (s.entryType(step, e) != kArray || !kindArray(step, e, slot, array)) return bad();
            const u32 list = s.entryValue(step, e);
            const int costAt = costs ? s.find(costs, s.entryKey(step, e)) : -1;
            const u32 cost = costAt < 0 ? 0 : s.entryValue(costs, static_cast<u32>(costAt));
            const u32 hashKey = static_cast<u32>(t.tableFind(t.keys, "Hash"));
            const u32 n = t.count(array), m = s.count(list);
            u32 adds = 0;
            for (u32 j = 0; j < m; ++j) {
                u32 hash = 0;
                if (s.itemType(list, j) != kDict || !hashOf(s, s.item(list, j), hash))
                    return w().refuse("the merge data is unreadable");
                if (!rowWithHash(array, hash, hashKey)) ++adds;
            }
            report.present += m - adds;
            if (!adds) continue;
            need += arrayBytes(std::size_t{n} + adds);
            const u32 grown = dry ? 0 : t.grownArray(array, adds);
            if (grown == kNone) return false;
            for (u32 j = 0, out = n; j < m; ++j) {
                const u32 row = s.item(list, j);
                u32 hash = 0, made = 0;
                hashOf(s, row, hash);
                if (rowWithHash(array, hash, hashKey)) continue;
                if (!copyValue(copier, kDict, row, made)) return false;
                const int file = [&] {
                    const int at = s.field(row, "SaveFileIndex", kInt);
                    return at < 0 ? -1 : static_cast<int>(s.entryValue(row, static_cast<u32>(at)));
                }();
                growth.offset += cost;
                growth.size += cost;
                if (file >= 0 && file < 16) {
                    growth.filePos[file] += cost;
                    growth.fileSize[file] += cost;
                }
                if (!dry) t.setItem(grown, out++, kDict, made);
            }
            if (!dry) t.setEntryValue(data, static_cast<u32>(slot), grown);
            report.added += adds;
        }
        return true;
    }

    // Appends to array field `name` of `row` the items of the merge-data array that `absent` accepts.
    template <typename Absent>
    bool appendTo(u32 row, const char* name, u32 source, Absent&& absent, u32& adds) {
        const int at = t.field(row, name, kArray);
        if (at < 0 || !t.container(t.entryValue(row, static_cast<u32>(at)), kArray)) return bad();
        const u32 array = t.entryValue(row, static_cast<u32>(at));
        const u32 n = t.count(array), m = s.count(source);
        adds = 0;
        for (u32 j = 0; j < m; ++j) adds += absent(array, j) ? 1 : 0;
        if (!adds) return true;
        need += arrayBytes(std::size_t{n} + adds) + 16 * std::size_t{adds};
        const u32 grown = dry ? 0 : t.grownArray(array, adds);
        if (grown == kNone) return false;
        for (u32 j = 0, out = n; j < m; ++j) {
            if (!absent(array, j)) continue;
            u32 made = 0;
            if (!copyValue(copier, s.itemType(source, j), s.item(source, j), made)) return false;
            if (!dry) t.setItem(grown, out++, s.itemType(source, j), made);
        }
        if (!dry) t.setEntryValue(row, static_cast<u32>(at), grown);
        return true;
    }

    template <typename Each>
    bool eachEdit(u32 step, Each&& each) {
        const u32 hashKey = static_cast<u32>(t.tableFind(t.keys, "Hash"));
        for (u32 e = 0, kinds = s.count(step); e < kinds; ++e) {
            int slot = 0;
            u32 array = 0;
            if (s.entryType(step, e) != kArray || !kindArray(step, e, slot, array)) return bad();
            const u32 list = s.entryValue(step, e);
            for (u32 i = 0, m = s.count(list); i < m; ++i) {
                u32 item = 0, row = 0;
                if (!edited(list, i, array, hashKey, item, row) || !each(item, row)) return false;
            }
        }
        return true;
    }

    bool choices(u32 step) {
        return eachEdit(step, [&](u32 item, u32 row) {
            const int names = s.field(item, "RawValues", kArray), values = s.field(item, "Values", kArray);
            const int have = t.field(row, "RawValues", kArray);
            if (names < 0 || values < 0) return w().refuse("the merge data is unreadable");
            if (have < 0) return bad();
            const u32 sNames = s.entryValue(item, static_cast<u32>(names));
            const u32 sValues = s.entryValue(item, static_cast<u32>(values));
            const u32 tNames = t.entryValue(row, static_cast<u32>(have));
            if (!s.container(sNames, kArray) || !s.container(sValues, kArray) || s.count(sNames) != s.count(sValues))
                return w().refuse("the merge data is unreadable");
            // The name list is read before either list grows, so both passes over it agree.
            const auto absent = [&](u32, u32 j) {
                const char* name = s.text(s.strings, s.item(sNames, j));
                for (u32 i = 0, n = t.count(tNames); i < n; ++i)
                    if (t.itemType(tNames, i) == kString && !std::strcmp(t.text(t.strings, t.item(tNames, i)), name))
                        return false;
                return true;
            };
            u32 addedValues = 0, addedNames = 0;
            if (!appendTo(row, "Values", sValues, absent, addedValues)) return false;
            if (!appendTo(row, "RawValues", sNames, absent, addedNames)) return false;
            report.added += addedNames;
            report.present += s.count(sNames) - addedNames;
            return true;
        });
    }

    bool entries(u32 step) {
        return eachEdit(step, [&](u32 item, u32 row) {
            const int from = s.field(item, "DefaultValue", kArray);
            if (from < 0) return w().refuse("the merge data is unreadable");
            const u32 source = s.entryValue(item, static_cast<u32>(from));
            const auto absent = [&](u32 array, u32 j) {
                u32 hash = 0, other = 0;
                if (!hashOf(s, s.item(source, j), hash)) return false;
                for (u32 i = 0, n = t.count(array); i < n; ++i)
                    if (t.itemType(array, i) == kDict && hashOf(t, t.item(array, i), other) && other == hash)
                        return false;
                return true;
            };
            u32 adds = 0;
            if (!appendTo(row, "DefaultValue", source, absent, adds)) return false;
            report.added += adds;
            report.present += s.count(source) - adds;
            return true;
        });
    }

    bool raise(u32 step) {
        return eachEdit(step, [&](u32 item, u32 row) {
            for (u32 e = 0, n = s.count(item); e < n; ++e) {
                const char* name = s.text(s.keys, s.entryKey(item, e));
                if (!std::strcmp(name, "Hash")) continue;
                const int at = t.field(row, name, kUInt);
                if (at < 0 || s.entryType(item, e) != kUInt) return bad();
                if (t.entryValue(row, static_cast<u32>(at)) >= s.entryValue(item, e)) {
                    ++report.present;
                    continue;
                }
                if (!dry) t.setEntryValue(row, static_cast<u32>(at), s.entryValue(item, e));
                ++report.added;
            }
            return true;
        });
    }

    bool lengths(u32 step) {
        return eachEdit(step, [&](u32 item, u32 row) {
            const int length = s.field(item, "Length", kUInt), bytes = s.field(item, "ElementBytes", kUInt);
            const int original = s.named(item, "OriginalSize");
            const int at = t.field(row, "DefaultValue", kArray);
            if (length < 0 || bytes < 0) return w().refuse("the merge data is unreadable");
            if (at < 0 || !t.container(t.entryValue(row, static_cast<u32>(at)), kArray)) return bad();
            const u32 array = t.entryValue(row, static_cast<u32>(at));
            const u32 n = t.count(array), want = s.entryValue(item, static_cast<u32>(length));
            if (n >= want) {
                ++report.present;
                return true;
            }
            if (!n) return bad();
            need += arrayBytes(want);
            const u32 added = want - n, cost = added * s.entryValue(item, static_cast<u32>(bytes));
            const int file = saveFile(row);
            if (file >= 0 && file < 16) {
                growth.size += cost;
                growth.fileSize[file] += cost;
            }
            ++report.added;
            if (dry) return true;
            const u32 grown = t.grownArray(array, added);
            if (grown == kNone) return false;
            for (u32 i = n; i < want; ++i) t.setItem(grown, i, t.itemType(array, n - 1), t.item(array, n - 1));
            t.setEntryValue(row, static_cast<u32>(at), grown);
            const int size = t.named(row, "OriginalSize");
            const auto whole = [](u8 type) { return type == kInt || type == kUInt; };
            if (original >= 0 && size >= 0 && whole(s.entryType(item, static_cast<u32>(original))) &&
                s.entryType(item, static_cast<u32>(original)) == t.entryType(row, static_cast<u32>(size)) &&
                t.entryValue(row, static_cast<u32>(size)) < s.entryValue(item, static_cast<u32>(original)))
                t.setEntryValue(row, static_cast<u32>(size), s.entryValue(item, static_cast<u32>(original)));
            return true;
        });
    }

    // Adds the collected growth to the save sizes: two numbers in place and two per-file lists rewritten.
    bool sizes(u32 meta) {
        const int offset = t.field(meta, "AllDataSaveOffset", kInt), size = t.field(meta, "AllDataSaveSize", kInt);
        const int filePos = t.field(meta, "SaveDataOffsetPos", kArray);
        const int fileSize = t.field(meta, "SaveDataSize", kArray);
        if (offset < 0 || size < 0 || filePos < 0 || fileSize < 0) return bad();
        const u32 lists[2] = {t.entryValue(meta, static_cast<u32>(filePos)), t.entryValue(meta, static_cast<u32>(fileSize))};
        for (const u32 list : lists) {
            if (!t.container(list, kArray) || t.count(list) > 16) return bad();
            for (u32 i = 0, n = t.count(list); i < n; ++i)
                if (t.itemType(list, i) != kInt) return bad();
            need += arrayBytes(t.count(list));
        }
        if (dry || (!growth.offset && !growth.size)) return true;
        t.setEntryValue(meta, static_cast<u32>(offset), t.entryValue(meta, static_cast<u32>(offset)) + growth.offset);
        t.setEntryValue(meta, static_cast<u32>(size), t.entryValue(meta, static_cast<u32>(size)) + growth.size);
        const int slots[2] = {filePos, fileSize};
        for (int which = 0; which < 2; ++which) {
            const u32 grown = t.grownArray(lists[which], 0);
            if (grown == kNone) return false;
            for (u32 i = 0, n = t.count(grown); i < n; ++i)
                t.setItem(grown, i, kInt, t.item(grown, i) + (which ? growth.fileSize[i] : growth.filePos[i]));
            t.setEntryValue(meta, static_cast<u32>(slots[which]), grown);
        }
        return true;
    }

    bool run(bool write) {
        dry = write ? false : true;
        copier.dry = dry;
        growth = {};
        need = 0;
        if (!write) report = {};
        else report.added = report.present = 0;
        const int dataAt = t.field(t.root, "Data", kDict), metaAt = t.field(t.root, "MetaData", kDict);
        if (t.d[t.root] != kDict || dataAt < 0 || metaAt < 0 || t.tableFind(t.keys, "Hash") < 0) return bad();
        data = t.entryValue(t.root, static_cast<u32>(dataAt));
        const u32 meta = t.entryValue(t.root, static_cast<u32>(metaAt));
        if (!t.container(data, kDict) || !t.container(meta, kDict)) return bad();
        const auto step = [&](const char* name) {
            const int at = s.field(s.root, name, kDict);
            return at < 0 ? 0u : s.entryValue(s.root, static_cast<u32>(at));
        };
        const u32 rowsStep = step("Rows"), choicesStep = step("Choices"), entriesStep = step("Entries");
        const u32 raiseStep = step("Raise"), lengthsStep = step("Lengths");
        if (rowsStep && !rows(rowsStep, step("Cost"))) return false;
        if (choicesStep && !choices(choicesStep)) return false;
        if (entriesStep && !entries(entriesStep)) return false;
        if (raiseStep && !raise(raiseStep)) return false;
        if (lengthsStep && !lengths(lengthsStep)) return false;
        return sizes(meta);
    }
};

}  // namespace detail

inline bool mergeGameData(Work& w, const u8* delta, std::size_t deltaSize, Report& report) {
    Byml t, s;
    if (!t.open(w.base, w.size, &w)) return w.refuse("the loaded file is not a save-flag list this build understands");
    if (!s.open(delta, deltaSize) || s.big != t.big || s.d[s.root] != kDict)
        return w.refuse("the merge data is unreadable");
    const std::size_t mark = w.top;
    u32* memo = w.scratchArray<u32>(deltaSize / 4 + 1);
    const char** names = w.scratchArray<const char*>(s.tableCount(s.strings) + 1);
    if (!memo || !names) return false;
    Copier copier{&t, &s, memo, true};
    detail::GameDataPass pass(t, s, copier, report);
    if (!pass.run(false)) return false;
    if (!report.added) {
        w.release(mark);
        return true;
    }
    const u32 k = missingStrings(t, s, names);
    if (stringRoom(t, names, k) + deltaSize + pass.need + 64 > w.room()) return w.refuse("no room left after the file");
    if (!addStrings(t, names, k) || !pass.run(true)) return false;
    report.strings = k;
    w.release(mark);
    return true;
}

}  // namespace fieldworks::merge
