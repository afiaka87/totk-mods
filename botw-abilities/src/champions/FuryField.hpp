// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "EffectPulse.hpp"
#include <algorithm>

namespace botw_abilities::champions {
// The native field's 50 m endpoint is verified; its one-second expansion is visual tuning.
inline float advanceFuryField(float elapsed, float seconds, bool visible) {
    return visible && std::isfinite(seconds) && seconds > 0
        ? std::min(1.0f, elapsed + seconds) : elapsed;
}

inline void releaseFuryField(EffectPulse& pulse) {
    if (!pulse.active()) { pulse.start(1.0f); return; }
    pulse.held = false;
    pulse.delay = 0;
    pulse.remaining = 1.0f;
}
}
