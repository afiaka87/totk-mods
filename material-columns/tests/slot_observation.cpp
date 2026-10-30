// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/native/SlotObservation.hpp"
#include <array>
#include <cassert>

int main() {
    using namespace material_columns;
    using namespace material_columns::native;
    std::array<std::uint8_t, 96> data{};
    const char* name = "Item_Fruit_A";
    std::memcpy(data.data(), &name, sizeof(name));
    auto quantity = [&](std::int32_t value) { std::memcpy(data.data() + 12, &value, sizeof(value)); };
    quantity(12);
    data[9] = 1;
    const SlotPresentation original(data.data());
    auto copied=data;
    std::array<char,64> sameName{};std::strcpy(sameName.data(),name);
    const char* movedName=sameName.data();std::memcpy(copied.data(),&movedName,sizeof(movedName));
    for(unsigned padding:{10u,11u,40u,42u,43u,66u,67u,73u,74u,75u,89u}) copied[padding]=99;
    assert(SlotPresentation(copied.data())==original); // Pointer and padding are not presentation.
    for(unsigned used:{8u,9u,12u,16u,20u,24u,28u,32u,36u,41u,44u,48u,52u,56u,60u,64u,65u,68u,72u,76u,80u,88u}) {
        copied=data;copied[used]^=1;
        assert(!(SlotPresentation(copied.data())==original));
    }
    copied=data;const char* other="Item_Fruit_B";std::memcpy(copied.data(),&other,sizeof(other));
    assert(!(SlotPresentation(copied.data())==original));
    auto value = observeSlot(data.data());
    assert(value.captured && value.eligible && value.quantity == 12 && value.item == identity(name));
    data[8] = 1; // A native checkmark does not by itself make the item invalid.
    assert(observeSlot(data.data()).eligible);
    for (auto count : {0, -1, -5}) {
        quantity(count);
        assert(!observeSlot(data.data()).eligible);
    }
    quantity(1);
    data[64] = 1;
    assert(!observeSlot(data.data()).eligible);
    data[64] = 0;
    data[24] = 4; // Native non-material blank/action pattern.
    assert(!observeSlot(data.data()).eligible);
    data[24] = 0;
    data[9] = 0;
    assert(!observeSlot(data.data()).eligible);
    assert(!observeSlot(nullptr).captured);
    name = "";
    std::memcpy(data.data(), &name, sizeof(name));
    assert(observeSlot(data.data()).item == 0);
    std::array<char, 64> unterminated{};
    unterminated.fill('a');
    name = unterminated.data();
    std::memcpy(data.data(), &name, sizeof(name));
    assert(!observeSlot(data.data()).eligible && observeSlot(data.data()).item == 0);
}
