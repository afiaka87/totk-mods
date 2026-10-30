// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "../pure/InventoryColumns.hpp"
#include "SlotObservation.hpp"

namespace material_columns::native {
bool installInventoryProbe(std::uintptr_t mainBase, bool validateOnly = false);
// Called on the menu thread after the stock material list has been populated.
// Entries carry actual native shortcut indices, not sorted catalog positions.
bool readInventory(std::uintptr_t parentScreen, InventoryEntry* entries,
                   std::size_t capacity, std::size_t& count);
bool readInventoryItem(std::uintptr_t parentScreen, std::uint32_t nativeIndex, SlotObservation& result);
}
