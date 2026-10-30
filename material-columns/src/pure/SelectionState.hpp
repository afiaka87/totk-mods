// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "InventoryColumns.hpp"

namespace material_columns {

// Serialized preferences contain identities only. Native slots belong to one snapshot.
template<std::size_t MaxGroups> struct Preferences {
    std::array<Preference, MaxGroups> items{};
    std::size_t count{};
    GroupOrder selected{};
    bool columns{};
    std::uint8_t stockPattern = 6;
};

template<std::size_t MaxItems, std::size_t MaxGroups> class SelectionState {
public:
    using Memory = Preferences<MaxGroups>;

    template<class Lookup>
    bool rebuild(const InventoryEntry* entries, std::size_t count, Lookup lookup,
                 CatalogMatch materialFallback, CatalogMatch deviceFallback) {
        // Within one live inventory retain the exact occurrence of a repeated type.
        // Across loads/reorders the durable actor preference remains the fallback.
        struct Previous { GroupId group{}; Item item{}; };
        std::array<Previous, MaxGroups> previous{};
        const auto previousCount = inventory_.groupCount();
        for (std::size_t i = 0; i < previousCount; ++i)
            if (const auto* current = item(i, positions_[i])) previous[i] = {groupId(i), *current};
        focused_ = no_index;
        if (!inventory_.rebuild(entries, count, lookup, materialFallback, deviceFallback)) return false;
        for (std::size_t i = 0; i < inventory_.groupCount(); ++i) {
            const auto column = inventory_.column(i);
            const auto* preferred = preference(column.group);
            positions_[i] = restore(column, preferred);
            for (std::size_t old = 0; preferred && old < previousCount; ++old) {
                if (previous[old].group != column.group || previous[old].item.id != preferred->item) continue;
                for (std::size_t row = 0; row < column.count; ++row)
                    if (column.items[row].id == previous[old].item.id &&
                        column.items[row].native_slot == previous[old].item.native_slot) positions_[i] = row;
            }
            const auto order = inventory_.group(i)->order;
            if (focused_ == no_index || order.id == memory_.selected.id ||
                (inventory_.group(focused_)->order.id != memory_.selected.id &&
                 distance(order.rank, memory_.selected.rank) <
                 distance(inventory_.group(focused_)->order.rank, memory_.selected.rank))) focused_ = i;
        }
        return true;
    }

    // Switching from a stock mode explicitly keeps that exact item when available.
    bool selectNative(std::uint32_t slot) {
        for (std::size_t i = 0; i < groupCount(); ++i) {
            const auto column = inventory_.column(i);
            for (std::size_t row = 0; row < column.count; ++row) {
                if (column.items[row].native_slot != slot) continue;
                if (!save(column, row)) return false;
                focused_ = i;
                positions_[i] = row;
                memory_.selected = inventory_.group(i)->order;
                return true;
            }
        }
        return false;
    }
    bool selectIdentity(ItemId id) {
        for (std::size_t i = 0; i < groupCount(); ++i) {
            const auto column = inventory_.column(i);
            for (std::size_t row = 0; row < column.count; ++row)
                if (column.items[row].id == id) return selectNative(column.items[row].native_slot);
        }
        return false;
    }

    bool horizontal(int direction) {
        const auto next = step(focused_, groupCount(), direction);
        if (next == no_index || next == focused_) return false;
        focused_ = next;
        memory_.selected = inventory_.group(next)->order;
        // Merely visiting a column does not erase its temporarily unavailable preference.
        return true;
    }
    bool vertical(int direction) {
        if (focused_ == no_index) return false;
        const auto column = inventory_.column(focused_);
        const auto next = step(positions_[focused_], column.count, direction);
        if (next == positions_[focused_] || !save(column, next)) return false;
        positions_[focused_] = next;
        memory_.selected = inventory_.group(focused_)->order;
        return true;
    }

    const Item* selected() const {
        return focused_ == no_index ? nullptr : item(focused_, positions_[focused_]);
    }
    const Item* item(std::size_t group, std::size_t row) const {
        const auto column = inventory_.column(group);
        return row < column.count ? &column.items[row] : nullptr;
    }
    Window view(std::size_t group) const {
        return group < groupCount() ? window(positions_[group], inventory_.column(group).count) : Window{};
    }
    bool bind(std::uint32_t logical, std::uint32_t& slot, bool& selectedRow) const {
        const auto group = logical / 3;
        const auto rows = view(group);
        const auto row = logical % 3 == 0 ? rows.center : logical % 3 == 1 ? rows.above : rows.below;
        const auto* value = item(group, row);
        if (!value) return false;
        slot = value->native_slot;
        selectedRow = group == focused_ && logical % 3 == 0;
        return true;
    }
    std::size_t focused() const { return focused_; }
    std::size_t groupCount() const { return inventory_.groupCount(); }
    GroupId groupId(std::size_t index) const { return inventory_.column(index).group; }
    const Memory& memory() const { return memory_; }
    void setColumns(bool value) { memory_.columns = value; }
    bool setStockPattern(unsigned value) {
        if (value < 6 || value > 9) return false;
        memory_.stockPattern = static_cast<std::uint8_t>(value);
        return true;
    }
    // Decoded input is validated before assignment; a corrupt load must not half-update state.
    bool load(const Memory& value) {
        if (value.count > MaxGroups || value.stockPattern < 6 || value.stockPattern > 9) return false;
        for (std::size_t i = 0; i < value.count; ++i) {
            if (!value.items[i].group || !value.items[i].item) return false;
            for (std::size_t j = 0; j < i; ++j)
                if (value.items[j].group == value.items[i].group) return false;
        }
        memory_ = value;
        inventory_.clear();
        focused_ = no_index;
        return true;
    }

private:
    const Preference* preference(GroupId group) const {
        for (std::size_t i = 0; i < memory_.count; ++i)
            if (memory_.items[i].group == group) return &memory_.items[i];
        return nullptr;
    }
    bool save(const Column& column, std::size_t row) {
        std::size_t i = 0;
        while (i < memory_.count && memory_.items[i].group != column.group) ++i;
        if (i == MaxGroups || !remember(column, row, memory_.items[i])) return false;
        if (i == memory_.count) ++memory_.count;
        return true;
    }
    InventoryColumns<MaxItems, MaxGroups> inventory_{};
    Memory memory_{};
    std::array<std::size_t, MaxGroups> positions_{};
    std::size_t focused_ = no_index;
};
} // namespace material_columns
