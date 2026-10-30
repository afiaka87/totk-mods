// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/pure/ColumnSelection.hpp"
#include <cassert>

using namespace material_columns;

int main() {
    const Item complete[]{{11, 10, 4}, {12, 20, 9}, {13, 30, 2}, {14, 40, 7}, {15, 50, 1}};
    const Item restricted[]{{11, 10, 4}, {13, 30, 2}, {15, 50, 1}};
    const Column full{101, complete, 5}, eligible{101, restricted, 3}, empty{101, nullptr, 0};
    Preference saved{101, 14, 40};
    assert(restore(full, &saved) == 3);
    assert(restore(eligible, &saved) == 1); // equal-distance tie uses earlier canonical item
    assert(saved.item == 14 && restore(full, &saved) == 3);
    assert(restore(empty, &saved) == no_index);
    assert(!remember(empty, 0, saved) && saved.item == 14);
    assert(remember(eligible, 2, saved) && saved.item == 15 && saved.rank == 50);
    assert(restore(full, &saved) == 4);
    const Preference unrelated{102, 11, 10};
    assert(restore(full, &unrelated) == 0);

    const Column other{102, restricted, 3};
    Preference other_saved{102, 11, 10};
    assert(remember(other, 1, other_saved));
    assert(restore(full, &saved) == 4 && restore(other, &other_saved) == 1);
    assert(full.items[restore(full, &saved)].native_slot == 1);
    assert(other.items[restore(other, &other_saved)].native_slot == 2);

    assert(step(0, 0, 1) == no_index);
    assert(step(0, 1, 1) == 0 && step(0, 1, -1) == 0);
    assert(step(4, 5, 1) == 4 && step(0, 5, -1) == 0);
    assert(step(2, 5, -1) == 1 && step(2, 5, 1) == 3);
    assert(step(2, 5, 0) == 2);

    const auto none = window(0, 0), one = window(0, 1), two = window(0, 2);
    assert(none.center == no_index && !none.overflow_above && !none.overflow_below);
    assert(one.above == no_index && one.below == no_index && !one.overflow_below);
    assert(two.above == no_index && two.below == 1 && !two.overflow_below);
    const auto three = window(1, 3), four = window(1, 4), five = window(2, 5);
    assert(three.above == 0 && three.center == 1 && three.below == 2);
    assert(!three.overflow_above && !three.overflow_below);
    assert(!four.overflow_above && four.overflow_below);
    assert(five.overflow_above && five.overflow_below);
    const auto last = window(4, 5), first = window(0, 5);
    assert(last.overflow_above && !last.overflow_below && last.below == no_index);
    assert(!first.overflow_above && first.overflow_below && first.above == no_index);
    assert(window(no_index, 5).center == no_index);
    assert(window(49, 100).overflow_above && window(49, 100).overflow_below);

}
