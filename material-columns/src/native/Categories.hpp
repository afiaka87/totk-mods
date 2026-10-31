// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "Catalog.hpp"
#include "../pure/CategoryConfig.hpp"

namespace material_columns::native::categories {
inline config::Registry registry;
static_assert(std::size(catalog::groups)+config::maxCategories<=96);
inline bool builtin(std::uint64_t id, std::uint32_t& rank) {
    const auto* group=catalogGroup(id);
    if (!group) return false;
    rank=group->rank; return true;
}
inline bool lookup(ItemId id, CatalogMatch& match) {
    const bool known=catalogLookup(id,match);
    const auto mapped=registry.mapped(id);
    if (!mapped) return known;
    std::uint32_t rank{};
    if (!registry.rank(mapped,rank,builtin)) return known;
    match={mapped,known ? match.item_rank : 0xffffffffu,rank}; return true;
}
void loadOnce(const char* language);
}
