// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <stdint.h>

namespace bivouac::refund {

enum class SlotNameDecision : uint8_t {
    RestoreEmptiedName,
    AddToMatchingSlot,
    RefuseDifferentItem,
};

constexpr bool textEquals(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return false;
    for (int i = 0; i < 64; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return false;
}

constexpr int firstFreeSlot(const uint32_t* pending, int capacity) {
    if (pending == nullptr || capacity <= 0) return -1;
    for (int i = 0; i < capacity; ++i) {
        if (pending[i] == 0) return i;
    }
    return -1;
}

inline int firstFreeSlot(const volatile uint32_t* pending, int capacity) {
    if (pending == nullptr || capacity <= 0) return -1;
    for (int i = 0; i < capacity; ++i) {
        if (pending[i] == 0) return i;
    }
    return -1;
}

constexpr int firstDueSlot(const uint32_t* pending, const int* dueTick,
                           int capacity, int now) {
    if (pending == nullptr || dueTick == nullptr || capacity <= 0) return -1;
    for (int i = 0; i < capacity; ++i) {
        if (pending[i] != 0 && now >= dueTick[i]) return i;
    }
    return -1;
}

inline int firstDueSlot(const volatile uint32_t* pending, const int* dueTick,
                        int capacity, int now) {
    if (pending == nullptr || dueTick == nullptr || capacity <= 0) return -1;
    for (int i = 0; i < capacity; ++i) {
        if (pending[i] != 0 && now >= dueTick[i]) return i;
    }
    return -1;
}

constexpr SlotNameDecision decideSlotName(const char* current, const char* expected) {
    if (current != nullptr && current[0] == '\0') return SlotNameDecision::RestoreEmptiedName;
    return textEquals(current, expected)
        ? SlotNameDecision::AddToMatchingSlot : SlotNameDecision::RefuseDifferentItem;
}

consteval bool contracts() {
    const uint32_t pending[4] = {1, 0, 1, 0};
    const int due[4] = {8, 0, 12, 0};
    if (firstFreeSlot(pending, 4) != 1) return false;
    if (firstFreeSlot(static_cast<const uint32_t*>(nullptr), 4) != -1) return false;
    if (firstDueSlot(pending, due, 4, 7) != -1) return false;
    if (firstDueSlot(pending, due, 4, 8) != 0) return false;
    if (firstDueSlot(pending, due, 4, 20) != 0) return false; // queue order stays FIFO-by-slot
    if (decideSlotName("", "Item_Fruit_P") != SlotNameDecision::RestoreEmptiedName) return false;
    if (decideSlotName("Item_Fruit_P", "Item_Fruit_P")
        != SlotNameDecision::AddToMatchingSlot) return false;
    if (decideSlotName("Item_Fruit_A", "Item_Fruit_P")
        != SlotNameDecision::RefuseDifferentItem) return false;
    return true;
}

static_assert(contracts(), "Bivouac refund policy contracts must hold");

} // namespace bivouac::refund
