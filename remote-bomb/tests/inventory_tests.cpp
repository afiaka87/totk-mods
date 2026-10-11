// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativeInventoryPolicy.hpp"
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <numeric>

namespace inv = remote_bomb::inventory;
unsigned checks{};
void require(bool okay, const char *message) {
    ++checks;
    if (!okay) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void stock() {
    require(!totk::engine::isPlausibleAddress(0x3812c74), "old aligned pointer check rejects native empty string");
    require(inv::validName(reinterpret_cast<const char *>(0x3812c74)), "native empty string is a valid byte address");
    require(inv::validName(reinterpret_cast<const char *>(0x3812c75)), "pooled names need no alignment");
    require(!inv::validName(nullptr), "null names are refused");
    require(!inv::validName(reinterpret_cast<const char *>(1ull << 40)), "unmapped names are refused");
    const char *sphere = remote_bomb::kActorNames[0][1];
    const char *cube = remote_bomb::kActorNames[1][1];
    const std::array<inv::Record, 5> records{{{"Item_Fruit_A", 7}, {"", -1}, {sphere, 0}, {cube, 1}, {sphere, 1}}};
    auto read = [&](unsigned i, inv::Record &record) { record = records[i]; return true; };
    require(inv::inspectStock(5, sphere, read) == inv::Status::Present, "scan passes empty and zero stock to avoid duplicate grants");
    require(inv::inspectStock(3, sphere, read) == inv::Status::Missing, "zero stock requires replenishment");
    require(inv::inspectStock(4, cube, read) == inv::Status::Present, "shapes retain independent stock");
    require(inv::inspectStock(513, sphere, read) == inv::Status::Unavailable, "invalid native capacity stops scan");
    require(inv::inspectStock(5, sphere, [](unsigned, inv::Record &) { return false; }) == inv::Status::Deferred,
            "incomplete scan cannot add duplicates");
    require(inv::queueCanFit(12, 2, 8, true), "exact queue room retains two entries headroom");
    require(!inv::queueCanFit(12, 3, 8, true), "insufficient queue room refuses write");
    require(!inv::queueCanFit(0, 0, 2, true), "zero capacity refuses write");
    require(!inv::queueCanFit(12, 0, 8, false), "unallocated queue refuses write");
    require(!inv::queueCanFit(12, 0xfffff, 8, true), "out-of-bounds queue cursor refuses write");
    const auto located = inv::locateStock(5, sphere, read);
    require(located.status == inv::Status::Present && located.slot == 4 && located.quantity == 1,
            "stock probe reports the stocked slot and quantity");
    const auto empty = inv::locateStock(3, sphere, read);
    require(empty.status == inv::Status::Missing && empty.slot == 2 && empty.quantity == 0,
            "stock probe reports a zero-quantity slot as missing");
    require(inv::locateStock(2, cube, read).slot == -1, "absent stock has no slot");
    const auto failed = inv::locateStock(5, sphere, [](unsigned i, inv::Record &record) {
        record = {"Item_Fruit_A", 1};
        return i < 3;
    });
    require(failed.status == inv::Status::Deferred && failed.failed == 3, "stock probe reports the unread slot");
}
void nodes() {
    require(inv::validNode(reinterpret_cast<const void *>(std::uintptr_t{0x9b6c4a54})), "native sort nodes need only 4-byte alignment");
    require(inv::validNode(reinterpret_cast<const void *>(std::uintptr_t{0x9b6c4a58})), "8-byte aligned nodes remain valid");
    require(!inv::validNode(reinterpret_cast<const void *>(std::uintptr_t{0x9b6c4a56})), "misaligned links are refused");
    require(!inv::validNode(reinterpret_cast<const void *>(std::uintptr_t{0x9b6c4a55})), "odd links are refused");
    require(!inv::validNode(nullptr), "null links are refused");
    require(!inv::validNode(reinterpret_cast<const void *>(1ull << 40)), "unmapped links are refused");
    // Native pool layout: 0x18-byte nodes from list+0x2C leave every link at 4 mod 8.
    struct alignas(8) Pool { inv::SortList list; std::uint32_t pad[5]; std::uint8_t bytes[3 * 0x18]; } pool{};
    static_assert(offsetof(Pool, bytes) == 0x2C);
    std::array<inv::SortNode *, 3> pooled{};
    for (unsigned i = 0; i < 3; ++i)
        pooled[i] = reinterpret_cast<inv::SortNode *>(pool.bytes + 0x18 * i);
    const std::array<const char *, 3> names{{"Item_Fruit_A", remote_bomb::kActorNames[1][1], remote_bomb::kActorNames[0][1]}};
    pool.list.count = 3;
    pool.list.root = {&pooled[2]->link, &pooled[0]->link};
    for (unsigned i = 0; i < 3; ++i) {
        pooled[i]->index = i;
        pooled[i]->link = {i ? &pooled[i - 1]->link : &pool.list.root, i < 2 ? &pooled[i + 1]->link : &pool.list.root};
    }
    require((reinterpret_cast<std::uintptr_t>(&pooled[0]->link) & 7) == 4, "fixture reproduces native link alignment");
    const auto classify = [&](unsigned i, unsigned &rank) { rank = inv::priority(names[i]); return true; };
    const auto plausible = [](const void *p) { return p != nullptr && (reinterpret_cast<std::uintptr_t>(p) & 3) == 0; };
    inv::ListReport report;
    require(inv::prioritizeList(&pool.list, classify, plausible, &report) == 1, "pooled native list is reordered");
    require(report.refusal == inv::ListRefusal::None && report.count == 3, "accepted list reports no refusal");
    require(report.position[0] == 2 && report.slot[0] == 2 && report.position[1] == 1 && report.slot[1] == 1,
            "report locates sphere and cube before reordering");
    require(pooled[0]->index == 2 && pooled[1]->index == 1 && pooled[2]->index == 0, "sphere then cube lead");
    const auto aligned8 = [](const void *p) { return p != nullptr && (reinterpret_cast<std::uintptr_t>(p) & 7) == 0; };
    require(inv::prioritizeList(&pool.list, classify, aligned8, &report) == -1 &&
            report.refusal == inv::ListRefusal::Link && report.step == 0,
            "old 8-byte predicate refusal is reported as a link refusal at the first node");
    require(inv::prioritizeList(&pool.list, [](unsigned, unsigned &) { return false; }, plausible, &report) == -1 &&
            report.refusal == inv::ListRefusal::Classify && report.step == 0 && report.index == 2,
            "name refusal reports the list step and slot");
    pool.list.count = 2;
    require(inv::prioritizeList(&pool.list, classify, plausible, &report) == -1 &&
            report.refusal == inv::ListRefusal::Closing && report.step == 2, "short count reports a closing refusal");
    pool.list.count = 513;
    require(inv::prioritizeList(&pool.list, classify, plausible, &report) == -1 &&
            report.refusal == inv::ListRefusal::Header && report.count == 513, "oversized count reports a header refusal");
}
void orders() {
    const std::array<const char *, 5> names{{"Item_Fruit_A", "Obj_RemoteBombCubePlus", "BombFruit",
                                          "Obj_RemoteBombSpherePlus", "SpObj_TimerBomb_Capsule_A_01"}};
    std::array<unsigned, 5> order{0, 1, 2, 3, 4};
    do {
        auto expected = order;
        std::stable_sort(expected.begin(), expected.end(), [&](unsigned a, unsigned b) {
            return inv::priority(names[a]) < inv::priority(names[b]);
        });
        inv::SortList list;
        std::array<inv::SortNode, 5> nodes{};
        list.count = 5;
        list.root = {&nodes.back().link, &nodes.front().link};
        for (unsigned i = 0; i < 5; ++i) {
            nodes[i].index = order[i];
            nodes[i].link = {i ? &nodes[i - 1].link : &list.root, i < 4 ? &nodes[i + 1].link : &list.root};
        }
        const auto classify = [&](unsigned i, unsigned &rank) { rank = inv::priority(names[i]); return true; };
        const auto valid = [](const void *p) { return p != nullptr; };
        require(inv::prioritizeList(&list, classify, valid) >= 0, "native list in consumer traversal order is accepted");
        for (unsigned i = 0; i < 5; ++i)
            require(nodes[i].index == expected[i], "every sort pins sphere/cube and preserves other ordering");
        require(inv::prioritizeList(&list, classify, valid) == 0, "pinning is idempotent");
        auto shortcut = order;
        inv::prioritize(shortcut.data(), 5, [&](unsigned i) { return inv::priority(names[i]); });
        require(shortcut == expected, "shortcut and main inventory order agree");
        nodes[2].link.previous = &list.root;
        require(inv::prioritizeList(&list, classify, valid) == -1, "corrupt native links are refused before edits");
    } while (std::next_permutation(order.begin(), order.end()));
    inv::SortList empty;
    empty.root = {&empty.root, &empty.root};
    require(inv::prioritizeList(&empty, [](unsigned, unsigned &) { return false; },
        [](const void *p) { return p != nullptr; }) == 0, "empty inventory leaves native list intact");
}
int main() { stock(); nodes(); orders(); std::cout << checks << " inventory checks passed\n"; }
