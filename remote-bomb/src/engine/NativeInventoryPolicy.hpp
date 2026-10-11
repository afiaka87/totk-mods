// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombIdentity.hpp"
#include "BombInventory.hpp"
#include "totk/engine/Pointer.hpp"
#include <array>
#include <cstddef>
#include <cstring>

namespace remote_bomb::inventory {
inline constexpr unsigned kMaterialCategory = 5, kMaximumItems = 512;
inline constexpr bool queueCanFit(int capacity, unsigned used, unsigned needed, bool allocated) {
    return allocated && capacity >= int(needed + 2) && used <= unsigned(capacity) - needed - 2;
}
struct Record { const char *name{}; int quantity{}; };
inline bool validName(const char *name) {
    return totk::engine::isPlausibleStringAddress(reinterpret_cast<std::uintptr_t>(name));
}
// Native Materials sort nodes come from a 0x18-stride pool at list+0x2C (1A76A28), so
// their links are only 4-byte aligned.
inline bool validNode(const void *address) {
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    return totk::engine::isPlausibleStringAddress(value) && (value & 3) == 0;
}
inline unsigned priority(const char *name) {
    const auto identity = identifyBomb(name);
    return identity ? (identity->shape == Shape::Sphere ? 0u : 1u) : 2u;
}
template <class Read>
Stock locateStock(unsigned count, const char *wanted, Read read) {
    if (count > kMaximumItems)
        return {Status::Unavailable};
    Stock found{Status::Missing};
    for (unsigned i = 0; i < count; ++i) {
        Record record;
        if (!read(i, record))
            return {Status::Deferred, -1, 0, i};
        if (std::strcmp(record.name, wanted))
            continue;
        if (record.quantity > 0)
            return {Status::Present, int(i), record.quantity};
        if (found.slot < 0)
            found = {Status::Missing, int(i), record.quantity};
    }
    return found;
}
template <class Read>
Status inspectStock(unsigned count, const char *wanted, Read read) {
    return locateStock(count, wanted, read).status;
}
template <class Value, class Rank>
bool prioritize(Value *values, unsigned count, Rank rank) {
    bool changed = false;
    for (unsigned i = 1; i < count; ++i) {
        const auto value = values[i];
        const auto order = rank(value);
        unsigned at = i;
        while (at && order < rank(values[at - 1])) {
            values[at] = values[at - 1];
            --at;
        }
        values[at] = value;
        changed |= at != i;
    }
    return changed;
}
struct SortLink { SortLink *previous{}, *next{}; };
struct SortNode { unsigned index{}, padding{}; SortLink link; };
struct SortList { SortLink root; int count{}; };
static_assert(offsetof(SortNode, link) == 8 && offsetof(SortList, count) == 16);
enum class ListRefusal : unsigned { None, Header, Link, Classify, Closing };
struct ListReport {
    ListRefusal refusal{};
    int step = -1, count = -1;
    std::uintptr_t detail{};
    unsigned index{};
    std::array<int, 2> position{-1, -1};
    std::array<unsigned, 2> slot{};
};
template <class Classify, class Validate>
int prioritizeList(SortList *list, Classify classify, Validate valid, ListReport *report = nullptr) {
    ListReport local;
    auto &out = report ? *report : local;
    out = {};
    const auto refuse = [&](ListRefusal reason, int step, const void *detail, unsigned index = 0) {
        out.refusal = reason;
        out.step = step;
        out.detail = reinterpret_cast<std::uintptr_t>(detail);
        out.index = index;
        return -1;
    };
    if (!valid(list))
        return refuse(ListRefusal::Header, -1, list);
    out.count = list->count;
    if (list->count < 0 || list->count > int(kMaximumItems))
        return refuse(ListRefusal::Header, -1, list);
    struct Item { unsigned index, rank; };
    std::array<SortNode *, kMaximumItems> nodes{};
    std::array<Item, kMaximumItems> items{};
    auto *link = list->root.next;
    auto *previous = &list->root;
    for (int i = 0; i < list->count; ++i) {
        if (!valid(link) || link == &list->root || link->previous != previous)
            return refuse(ListRefusal::Link, i, link);
        auto *node = reinterpret_cast<SortNode *>(reinterpret_cast<std::uintptr_t>(link) - 8);
        unsigned rank{};
        if (!classify(node->index, rank))
            return refuse(ListRefusal::Classify, i, node, node->index);
        if (rank < 2 && out.position[rank] < 0) {
            out.position[rank] = i;
            out.slot[rank] = node->index;
        }
        nodes[i] = node;
        items[i] = {node->index, rank};
        previous = link;
        link = link->next;
    }
    if (link != &list->root || list->root.previous != previous)
        return refuse(ListRefusal::Closing, list->count, link);
    if (!prioritize(items.data(), unsigned(list->count), [](Item item) { return item.rank; }))
        return 0;
    for (int i = 0; i < list->count; ++i)
        nodes[i]->index = items[i].index;
    return 1;
}
}
