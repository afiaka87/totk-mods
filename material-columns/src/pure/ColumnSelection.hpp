// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>

namespace material_columns {

using ItemId = std::uint64_t;
using GroupId = std::uint64_t;
inline constexpr std::size_t no_index = static_cast<std::size_t>(-1);

// IDs are stable catalog identities; native_slot is meaningful only in this snapshot.
struct Item {
    ItemId id;
    std::uint32_t rank;
    std::uint32_t native_slot;
};

struct Preference {
    GroupId group;
    ItemId item;
    std::uint32_t rank;
};

// Members are already filtered for the current context and in canonical type order.
struct Column {
    GroupId group;
    const Item* items;
    std::size_t count;
};

constexpr std::uint32_t distance(std::uint32_t a, std::uint32_t b) {
    return a > b ? a - b : b - a;
}

// Temporary context/depletion fallback must not overwrite the durable preference.
constexpr std::size_t restore(const Column& column, const Preference* preferred) {
    if (!column.count) return no_index;
    if (!preferred || preferred->group != column.group) return 0;
    std::size_t nearest = 0;
    for (std::size_t i = 0; i < column.count; ++i) {
        if (column.items[i].id == preferred->item) return i;
        if (distance(column.items[i].rank, preferred->rank) <
            distance(column.items[nearest].rank, preferred->rank)) nearest = i;
    }
    return nearest;
}

constexpr std::size_t step(std::size_t index, std::size_t count, int direction) {
    if (!count) return no_index;
    if (index >= count) index = 0;
    if (direction < 0 && index) return index - 1;
    if (direction > 0 && index + 1 < count) return index + 1;
    return index;
}

struct Window {
    std::size_t above = no_index;
    std::size_t center = no_index;
    std::size_t below = no_index;
    bool overflow_above = false;
    bool overflow_below = false;
};

constexpr Window window(std::size_t index, std::size_t count) {
    if (!count || index >= count) return {};
    return {index ? index - 1 : no_index, index, index + 1 < count ? index + 1 : no_index,
            index > 1, count - index > 2};
}

// Only explicit navigation records a replacement preference.
constexpr bool remember(const Column& column, std::size_t index, Preference& destination) {
    if (index >= column.count) return false;
    destination = {column.group, column.items[index].id, column.items[index].rank};
    return true;
}

// Keep category identity across inventory changes; otherwise choose the nearest surviving rank.
struct GroupOrder {
    GroupId id;
    std::uint32_t rank;
};


} // namespace material_columns
