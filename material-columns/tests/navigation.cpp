// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/pure/Navigation.hpp"
#include <cassert>
#include <limits>
using namespace material_columns;
int main() {
    Navigation navigation;
    auto right = [&] (float dt = 1) { return navigation.update(1, 0, false, true, false, false, dt, 12, 3); };
    assert(right().direction == 1);
    for (int i = 0; i < 10; ++i) assert(right().axis == Axis::none);
    assert(right().axis == Axis::horizontal); // total 12 native frames including initial press
    assert(right().axis == Axis::none && right().axis == Axis::none);
    assert(right().direction == 1);
    navigation.reset();
    auto move = navigation.update(0.8f, 0.7f, false, true, true, false, 1, 12, 3);
    assert(move.axis == Axis::horizontal);
    // Tilting farther up during the same diagonal gesture cannot also change rows.
    move = navigation.update(0.7f, 0.8f, false, true, true, false, 11, 12, 3);
    assert(move.axis == Axis::horizontal);
    move = navigation.update(0, 1, false, false, true, false, 1, 12, 3);
    assert(move.axis == Axis::vertical && move.direction == -1);
    move = navigation.update(0, -1, false, false, false, true, 1, 12, 3);
    assert(move.axis == Axis::vertical && move.direction == 1);
    assert(navigation.update(0, 0, false, false, false, false, 1, 12, 3).axis == Axis::none);
    assert(right(0.5f).direction == 1);
    for (int i = 0; i < 22; ++i) assert(right(0.5f).axis == Axis::none);
    assert(right(0.5f).direction == 1);
    assert(right(std::numeric_limits<float>::quiet_NaN()).axis == Axis::none);
    assert(right().direction == 1);
    navigation.reset();
    // Processed native dead zone wins over a nonzero raw stick value.
    assert(navigation.update(.1f, .1f, false, false, false, false, 1, 12, 3).axis == Axis::none);
}
