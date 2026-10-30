// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/pure/SelectionState.hpp"
#include <cassert>
#include <algorithm>
using namespace material_columns;

bool lookup(ItemId id, CatalogMatch& match) {
    if (id < 10 || id >= 30) return false;
    match = {id / 10, static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(id / 10 * 10)};
    return true;
}
int main() {
    SelectionState<32, 8> state;
    InventoryEntry entries[]{{13, 0, false, true}, {22, 1, false, true}, {10, 2, false, true},
        {12, 3, false, true}, {21, 4, false, true}, {11, 5, false, true}};
    const CatalogMatch other{3, 100, 100}, device{4, 200, 200};
    auto rebuild = [&] { assert(state.rebuild(entries, 6, lookup, other, device)); };
    rebuild();
    assert(state.selected()->id == 10);
    assert(state.selectNative(0) && state.selected()->id == 13);
    assert(state.horizontal(1) && state.selected()->id == 21);
    assert(state.vertical(1) && state.selected()->id == 22);
    assert(!state.vertical(1) && !state.horizontal(1));
    assert(state.horizontal(-1) && state.selected()->id == 13);
    std::uint32_t slot = 999;
    bool selected = false;
    assert(state.bind(0, slot, selected) && slot == 0 && selected);
    assert(state.bind(1, slot, selected) && slot == 3 && !selected);
    assert(!state.bind(2, slot, selected));
    assert(state.bind(3, slot, selected) && slot == 1 && !selected);
    assert(state.view(0).overflow_above && !state.view(0).overflow_below);

    // Restricted action context temporarily removes the remembered last item.
    entries[0].eligible = false;
    rebuild();
    assert(state.selected()->id == 12 && state.memory().items[0].item == 13);
    assert(state.horizontal(1) && state.horizontal(-1));
    assert(state.memory().items[0].item == 13);
    entries[0].eligible = true;
    // Native sorting changes every action token; identity and both columns must survive.
    for (auto& entry : entries) entry.native_slot += 100;
    std::reverse(std::begin(entries), std::end(entries));
    rebuild();
    assert(state.selected()->id == 13 && state.selected()->native_slot == 100);
    assert(state.horizontal(1) && state.selected()->id == 22);
    const auto saved = state.memory();
    SelectionState<32, 8> restarted;
    assert(restarted.load(saved));
    assert(restarted.rebuild(entries, 6, lookup, other, device));
    assert(restarted.selected()->id == 22);
    // Empty groups can disappear and return without replacing persistent selection.
    for (auto& entry : entries) if (entry.id >= 20) entry.eligible = false;
    rebuild();
    assert(state.selected()->id == 13 && state.memory().selected.id == 2);
    for (auto& entry : entries) entry.eligible = true;
    rebuild();
    assert(state.selected()->id == 22);
    assert(state.selectIdentity(12) && state.selected()->native_slot == 103);
    assert(!state.selectIdentity(99) && state.selected()->id == 12);
    auto corrupt = saved;
    corrupt.items[1].group = corrupt.items[0].group;
    assert(!state.load(corrupt) && state.selected()->id == 12);
    assert(state.rebuild(nullptr, 0, lookup, other, device));
    assert(!state.selected() && !state.vertical(1) && !state.horizontal(1));
    assert(!state.bind(0, slot, selected));
    // Two Fans (same actor type) must remain independently navigable after each refresh.
    InventoryEntry repeated[]{{10, 152, true, true}, {10, 153, true, true}, {22, 154, true, true}};
    assert(state.rebuild(repeated, 3, lookup, other, device));
    assert(state.selectNative(152) && state.vertical(1));
    assert(state.selected()->native_slot == 153);
    assert(state.rebuild(repeated, 3, lookup, other, device));
    assert(state.selected()->native_slot == 153);
    assert(state.bind(0, slot, selected) && slot == 153 && selected);
    assert(state.horizontal(1));
    assert(state.rebuild(repeated, 3, lookup, other, device));
    assert(state.horizontal(-1) && state.selected()->native_slot == 153);
    // Once that slot changes type, restore the remembered type at its remaining slot.
    repeated[1].id = 11;
    assert(state.rebuild(repeated, 3, lookup, other, device));
    assert(state.selected()->id == 10 && state.selected()->native_slot == 152);
    static_assert(sizeof(SelectionState<1024, 96>) < 32 * 1024);
}
