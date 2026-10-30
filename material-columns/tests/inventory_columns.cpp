// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/pure/InventoryColumns.hpp"
#include <cassert>
#include <algorithm>

using namespace material_columns;
constexpr GroupId fruit = identity("group:fruit");
constexpr GroupId eyes = identity("group:eyeballs");
constexpr GroupId devices = identity("group:zonai-devices");
constexpr CatalogMatch fallback{identity("group:other-materials"), 0xffffffffu, 100};
constexpr CatalogMatch deviceFallback{devices, 0xffffffffu, 90};

bool lookup(ItemId id, CatalogMatch& result) {
    switch (id) {
    case 10: result = {fruit, 1, 0}; return true;
    case 11: result = {fruit, 2, 0}; return true;
    case 20: result = {eyes, 10, 10}; return true;
    case 21: result = {eyes, 11, 10}; return true;
    case 30: result = {devices, 90, 90}; return true;
    default: return false;
    }
}

int main() {
    static_assert(identity("a") == 0xaf63dc4c8601ec8cULL);
    static_assert(sizeof(InventoryColumns<1024, 96>) < 28 * 1024);
    InventoryColumns<16, 8> snapshot;
    InventoryEntry source[] = {{20, 7, false, true}, {11, 3, false, true},
        {10, 8, false, true}, {21, 2, false, false}, {30, 5, true, true},
        {40, 9, true, true}, {50, 1, false, true}};
    assert(snapshot.rebuild(source, 7, lookup, fallback, deviceFallback));
    assert(snapshot.itemCount() == 6 && snapshot.groupCount() == 4);
    assert(snapshot.column(0).group == fruit && snapshot.column(0).count == 2);
    assert(snapshot.column(0).items[0].id == 10 && snapshot.column(0).items[0].native_slot == 8);
    assert(snapshot.column(1).group == eyes && snapshot.column(1).count == 1);
    assert(snapshot.column(2).group == devices && snapshot.column(2).count == 2);
    assert(snapshot.column(3).group == fallback.group);
    const Preference preferred{eyes, 21, 11};
    assert(restore(snapshot.column(1), &preferred) == 0);
    source[3].eligible = true;
    std::reverse(std::begin(source), std::end(source));
    assert(snapshot.rebuild(source, 7, lookup, fallback, deviceFallback));
    assert(snapshot.column(1).items[restore(snapshot.column(1), &preferred)].native_slot == 2);
    // Losing the known device must not move the mod-added device's group behind fallback.
    for (auto& entry : source) if (entry.id == 30) entry.eligible = false;
    assert(snapshot.rebuild(source, 7, lookup, fallback, deviceFallback));
    assert(snapshot.column(2).group == devices && snapshot.group(2)->order.rank == 90);
    // Repeated actor names retain both exact native action slots in deterministic order.
    InventoryEntry duplicate[] = {{10, 2, false, true}, {10, 1, false, true}};
    assert(snapshot.rebuild(duplicate, 2, lookup, fallback, deviceFallback));
    assert(snapshot.itemCount() == 2 && snapshot.column(0).count == 2);
    assert(snapshot.column(0).items[0].native_slot == 1);
    assert(snapshot.column(0).items[1].native_slot == 2);
    duplicate[1] = {11, 2, false, true};
    assert(!snapshot.rebuild(duplicate, 2, lookup, fallback, deviceFallback));
    assert(snapshot.rebuild(nullptr, 0, lookup, fallback, deviceFallback));
    assert(snapshot.column(0).count == 0 && snapshot.group(0) == nullptr);
    assert(!snapshot.rebuild(source, 17, lookup, fallback, deviceFallback));
    InventoryColumns<16, 1> tooFewGroups;
    assert(!tooFewGroups.rebuild(source, 7, lookup, fallback, deviceFallback));
    assert(tooFewGroups.itemCount() == 0);
}
