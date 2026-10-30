// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "ColumnSelection.hpp"
#include "Identity.hpp"
#include <array>

namespace material_columns {

struct CatalogMatch {
    GroupId group;
    std::uint32_t item_rank;
    std::uint32_t group_rank;
};

struct InventoryEntry {
    ItemId id;
    std::uint32_t native_slot;
    bool device;
    bool eligible;
    std::uint32_t usage{};
};

// Snapshot storage is bounded by the adapter's verified native inventory capacity.
// The adapter rebuilds after quantity/context/order changes, never persisting slots.
template<std::size_t MaxItems, std::size_t MaxGroups>
class InventoryColumns {
public:
    struct Group {
        GroupOrder order{};
        std::size_t begin{};
        std::size_t count{};
        std::uint64_t usage{};
    };

    template<class Lookup>
    bool rebuild(const InventoryEntry* source, std::size_t count, Lookup lookup,
                 CatalogMatch unknownMaterial, CatalogMatch unknownDevice) {
        clear();
        if (count > MaxItems || (count && !source)) return false;
        for (std::size_t i = 0; i < count; ++i) {
            if (!source[i].eligible) continue;
            CatalogMatch match{};
            if (!lookup(source[i].id, match)) {
                match = source[i].device ? unknownDevice : unknownMaterial;
            }
            // Actor identity names an item type, not a unique native inventory record.
            // Keep repeated types as separate slots; never merge quantities or action tokens.
            bool firstType = true;
            for (std::size_t j = 0; j < size_; ++j) {
                if (items_[j].id == source[i].id) firstType = false;
                if (items_[j].native_slot == source[i].native_slot) {
                    clear();
                    return false;
                }
            }
            std::size_t group = 0;
            while (group < groups_count_ && groups_[group].order.id != match.group) ++group;
            if (group == groups_count_) {
                if (groups_count_ == MaxGroups) { clear(); return false; }
                groups_[groups_count_++] = {{match.group, match.group_rank}, 0, 0, 0};
            } else if (match.group_rank != groups_[group].order.rank) {
                clear();
                return false;
            }
            // Usage belongs to the actor type, even when native inventory has repeated records.
            if (firstType) groups_[group].usage += source[i].usage;
            items_[size_] = {source[i].id, match.item_rank, source[i].native_slot};
            item_groups_[size_++] = match.group;
        }
        // Small fixed sets: insertion sort avoids temporary buffers/heap allocations.
        for (std::size_t i = 1; i < groups_count_ && i < MaxGroups; ++i) {
            auto value = groups_[i];
            auto j = i;
            while (j && groupBefore(value, groups_[j - 1])) {
                groups_[j] = groups_[j - 1];
                --j;
            }
            groups_[j] = value;
        }
        for (std::size_t i = 0; i < size_; ++i) item_groups_[i] = groupIndex(item_groups_[i]);
        for (std::size_t i = 1; i < size_; ++i) {
            const auto value = items_[i];
            const auto group = item_groups_[i];
            auto j = i;
            while (j && itemBefore(value, group, items_[j - 1], item_groups_[j - 1])) {
                items_[j] = items_[j - 1];
                item_groups_[j] = item_groups_[j - 1];
                --j;
            }
            items_[j] = value;
            item_groups_[j] = group;
        }
        std::size_t begin = 0;
        for (std::size_t i = 0; i < groups_count_; ++i) {
            groups_[i].begin = begin;
            while (begin < size_ && item_groups_[begin] == i) ++begin;
            groups_[i].count = begin - groups_[i].begin;
        }
        return true;
    }

    std::size_t groupCount() const { return groups_count_; }
    std::size_t itemCount() const { return size_; }
    const Group* group(std::size_t index) const { return index < groups_count_ ? &groups_[index] : nullptr; }
    Column column(std::size_t index) const {
        if (index >= groups_count_) return {};
        const auto& g = groups_[index];
        return {g.order.id, items_.data() + g.begin, g.count};
    }
    void clear() { size_ = groups_count_ = 0; }
    static constexpr std::uint32_t unknown_rank = 0xffffffffu;

private:
    static bool groupBefore(const Group& a, const Group& b) {
        if (a.usage != b.usage) return a.usage > b.usage;
        return a.order.rank < b.order.rank || (a.order.rank == b.order.rank && a.order.id < b.order.id);
    }
    std::size_t groupIndex(GroupId id) const {
        for (std::size_t i = 0; i < groups_count_; ++i) if (groups_[i].order.id == id) return i;
        return groups_count_;
    }
    static bool itemBefore(const Item& a, GroupId aGroup, const Item& b, GroupId bGroup) {
        if (aGroup != bGroup) return aGroup < bGroup;
        return a.rank < b.rank || (a.rank == b.rank &&
            (a.id < b.id || (a.id == b.id && a.native_slot < b.native_slot)));
    }
    std::array<Item, MaxItems> items_{};
    std::array<GroupId, MaxItems> item_groups_{};
    std::array<Group, MaxGroups> groups_{};
    std::size_t size_{};
    std::size_t groups_count_{};
};
} // namespace material_columns
