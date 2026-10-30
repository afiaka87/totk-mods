// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "../pure/InventoryColumns.hpp"
#include "MaterialCatalog.hpp"
#include <iterator>

namespace material_columns::native {
inline const catalog::Group* catalogGroup(GroupId id) {
    for (const auto& group : catalog::groups) if (group.id == id) return &group;
    return nullptr;
}
inline bool catalogLookup(ItemId id, CatalogMatch& match) {
    std::size_t first = 0, end = std::size(catalog::items);
    while (first < end) {
        const auto middle = first + (end - first) / 2;
        if (catalog::items[middle].item < id) first = middle + 1;
        else end = middle;
    }
    if (first == std::size(catalog::items) || catalog::items[first].item != id) return false;
    const auto& item = catalog::items[first];
    const auto* group = catalogGroup(item.group);
    if (!group) return false;
    match = {item.group, item.rank, group->rank};
    return true;
}
inline CatalogMatch fallback(bool device) {
    const auto id = identity(device ? "group:zonai-devices" : "group:other-materials");
    const auto* group = catalogGroup(id);
    return {id, 0xffffffffu, group ? group->rank : 0xffffffffu};
}
}
