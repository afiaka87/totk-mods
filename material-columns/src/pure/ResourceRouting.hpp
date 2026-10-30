// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace material_columns::resources {
// FindResourceByName reverses this integer before writing the four directory bytes.
inline constexpr std::uint32_t layoutType = 0x626c7974; // blyt
enum class LayoutId { None, Parent, Columns, Sort, Slot };

constexpr LayoutId layoutFor(std::uint32_t type, std::string_view name) {
    if (type != layoutType) return LayoutId::None;
    if (name == "ShortCut_00.bflyt") return LayoutId::Parent;
    if (name == "MCColumns_00.bflyt") return LayoutId::Columns;
    if (name == "PaShortCutSortWindow_00.bflyt") return LayoutId::Sort;
    if (name == "MCSlot_00.bflyt") return LayoutId::Slot;
    return LayoutId::None;
}

// The animation accessor chooses an archive by layout name before resolving anim/name_tag.bflan.
inline const char* animationLayout(const char* name) {
    return name && std::string_view(name) == "MCColumns_00" ? "PaShortCutSlideList_00" : name;
}
} // namespace material_columns::resources
