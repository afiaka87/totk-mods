// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>

namespace material_columns::native {
inline constexpr std::size_t maximumColumns = 96;
// logical / 3 is a column; logical % 3 is center, preceding, following.
// False leaves that slot clear. True returns an actual native material shortcut index.
using BindColumnItem = bool (*)(void* context, std::uint32_t logical,
                               std::uint32_t& nativeIndex, bool& selected);
struct ColumnAppearance { const char* label{}; bool above{}, below{}; };
using DescribeColumn = ColumnAppearance (*)(std::size_t group);
bool installColumnPanel(std::uintptr_t mainBase, bool validateOnly = false);
// The existing setSlot hook skips unchanged pooled records, preserving async icon work.
bool retainPooledSlot(std::uintptr_t slot, const void* parameters);
bool columnPanelReady(std::uintptr_t screen);
bool configureColumnPanel(std::uintptr_t screen, std::size_t count, std::size_t focusedColumn,
                          BindColumnItem bind, void* context, DescribeColumn describe);
void animateColumnMove(std::uintptr_t screen, std::size_t group, int direction);
void hideColumnPanel(std::uintptr_t screen);
bool focusNativeItem(std::uintptr_t screen, std::uint32_t nativeIndex, bool refreshCaption);
void playColumnMoveSound(std::uintptr_t screen, bool forward);
}
