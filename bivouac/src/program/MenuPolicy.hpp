// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

namespace bivouac::menu {

inline constexpr int kStarvationSaturation = 1000000;

struct Step {
    int starvedTicks = 0;
    bool opened = false;
    bool closed = false;
};

constexpr Step update(int previousStarvedTicks, bool workerAlive, int closeThreshold) {
    Step result{};
    if (!workerAlive) {
        result.opened = previousStarvedTicks == 0;
        result.starvedTicks = previousStarvedTicks < kStarvationSaturation
            ? previousStarvedTicks + 1 : kStarvationSaturation;
        return result;
    }
    result.closed = previousStarvedTicks >= closeThreshold;
    return result;
}

constexpr bool fullScreenMenuOpen(int starvedTicks, int threshold) {
    return starvedTicks >= threshold;
}

consteval bool contracts() {
    Step s = update(0, false, 4);
    if (!s.opened || s.closed || s.starvedTicks != 1) return false;
    s = update(3, true, 4);
    if (s.closed || s.starvedTicks != 0) return false;
    s = update(4, true, 4);
    if (!s.closed || s.starvedTicks != 0) return false;
    s = update(kStarvationSaturation, false, 4);
    if (s.starvedTicks != kStarvationSaturation) return false;
    if (!fullScreenMenuOpen(4, 4) || fullScreenMenuOpen(3, 4)) return false;
    return true;
}

static_assert(contracts(), "Bivouac menu policy contracts must hold");

} // namespace bivouac::menu
