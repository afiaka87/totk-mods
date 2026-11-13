// Copyright (c) HanSoloBlaster, afiaka87
// Small made-up inputs for the shared-file merge code. The real-file check is scripts/merge_check.py.
#include <cstdio>
#include <vector>

#include "pure/merge/Manifest.hpp"
#include "pure/merge/SizeTable.hpp"
#include "pure/merge/Zstd.hpp"

using namespace fieldworks::merge;

static int g_failed = 0;
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::printf("FAILED line %d: %s\n", __LINE__, #condition); \
            ++g_failed; \
        } \
    } while (false)

static void put32(std::vector<u8>& out, u32 value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(value >> (8 * i)));
}

// A size table with the given {name hash, bytes} entries and no named entries.
static std::vector<u8> table(std::initializer_list<std::pair<u32, u32>> entries) {
    std::vector<u8> out{'R', 'E', 'S', 'T', 'B', 'L'};
    put32(out, 1);
    put32(out, 160);
    put32(out, static_cast<u32>(entries.size()));
    put32(out, 0);
    for (const auto& [hash, size] : entries) put32(out, hash), put32(out, size);
    return out;
}

static std::vector<u8> additions(std::initializer_list<std::pair<u32, u32>> entries) {
    std::vector<u8> out{'F', 'W', 'R', 'T'};
    put32(out, static_cast<u32>(entries.size()));
    for (const auto& [hash, size] : entries) put32(out, hash), put32(out, size);
    return out;
}

static void sizeTable() {
    const u32 shared = crc32("RSDB/Shared.byml", 16);
    std::vector<std::pair<u32, u32>> rows{{10, 100}, {30, 300}, {shared, 5000}};
    if (rows[2].first < rows[1].first) std::swap(rows[1], rows[2]);
    std::vector<u8> file = table({rows[0], rows[1], rows[2]});
    const std::size_t used = file.size();
    file.resize(used + 64, 0xcd);
    // 10 is raised, 20 is new, 30 is already larger, and the shared file takes the override over 90000.
    const std::vector<u8> delta = additions({{10, 150}, {20, 200}, {30, 250}, {shared, 90000}});
    const Budget budget{"RSDB/Shared.byml", 7000};
    Work work(file.data(), used, file.size());
    Report report;
    CHECK(mergeSizeTable(work, delta.data(), delta.size(), &budget, 1, report));
    CHECK(work.size == used + 8);
    CHECK(budgetOf(file.data(), work.size, "RSDB/Shared.byml") == 7000);
    CHECK(rd32(file.data() + 14, false) == 4);
    u32 last = 0;
    for (u32 i = 0; i < 4; ++i) {
        const u32 hash = rd32(file.data() + 22 + 8 * i, false), size = rd32(file.data() + 26 + 8 * i, false);
        CHECK(!i || hash > last);
        last = hash;
        if (hash == 10) CHECK(size == 150);
        if (hash == 20) CHECK(size == 200);
        if (hash == 30) CHECK(size == 300);
    }
    // A second pass changes nothing, and a table with no room for the new entry is left as it was.
    const std::vector<u8> once(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(work.size));
    Work again(file.data(), work.size, file.size());
    CHECK(mergeSizeTable(again, delta.data(), delta.size(), &budget, 1, report) && again.size == once.size());
    CHECK(std::equal(once.begin(), once.end(), file.begin()));
    std::vector<u8> tight = table({rows[0], rows[1], rows[2]});
    const std::vector<u8> before = tight;
    Work none(tight.data(), tight.size(), tight.size());
    CHECK(!mergeSizeTable(none, delta.data(), delta.size(), &budget, 1, report) && none.refusal && tight == before);
}

static void smallParts() {
    // zstd frame heads: single-segment with a 4-byte size; a window byte then a 2-byte size (+256).
    const u8 single[] = {0x28, 0xb5, 0x2f, 0xfd, 0xa0, 0x78, 0x56, 0x34, 0x12};
    const u8 windowed[] = {0x28, 0xb5, 0x2f, 0xfd, 0x40, 0x58, 0x10, 0x00};
    const u8 unknown[] = {0x28, 0xb5, 0x2f, 0xfd, 0x00, 0x58, 0x10, 0x00};
    u64 size = 0;
    CHECK(frameContentSize(single, sizeof(single), size) && size == 0x12345678);
    CHECK(frameContentSize(windowed, sizeof(windowed), size) && size == 0x10 + 256);
    CHECK(!frameContentSize(unknown, sizeof(unknown), size));
    CHECK(!frameContentSize(single, 5, size));

    CHECK(pathNames("RSDB/Tag.Product.121.rstbl.byml.zs", "RSDB/Tag.Product.121.rstbl.byml"));
    CHECK(pathNames("content:/RSDB/Tag.Product.121.rstbl.byml", "RSDB/Tag.Product.121.rstbl.byml"));
    CHECK(!pathNames("RSDB/Tag.Product.121.rstbl.byml.zs.bak", "RSDB/Tag.Product.121.rstbl.byml"));
    CHECK(!pathNames("RSDB/Tag.Product.120.rstbl.byml.zs", "RSDB/Tag.Product.121.rstbl.byml"));

    Entry entry;
    entry.eighths = 8;
    entry.fixed = 100;
    CHECK(plannedBytes(entry, 1000) == 2112);
    entry.eighths = 0;
    CHECK(plannedBytes(entry, 1000) == 1120);
}

int main() {
    sizeTable();
    smallParts();
    std::printf(g_failed ? "%d checks failed\n" : "merge checks passed\n", g_failed);
    return g_failed ? 1 : 0;
}
