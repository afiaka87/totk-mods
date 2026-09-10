#pragma once
#include <algorithm>
#include <initializer_list>
#include "RecallHistory.hpp"

namespace self_recall::pure {
inline bool motionDiscontinuity(totk::core::WorldPosition from,
        totk::core::WorldPosition to, const float previousVelocity[3],
        const float currentVelocity[3], double seconds, float threshold) {
    if (!isTeleportDiscontinuity(distance3(from, to), threshold)) return false;
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > 1) return true;
    for (const auto* velocity : {previousVelocity, currentVelocity}) {
        if (!velocity) continue;
        double travel[3], length2 = 0, dot = 0;
        const double delta[]{to.x - from.x, to.y - from.y, to.z - from.z};
        bool valid = true;
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(velocity[i])) { valid = false; break; }
            travel[i] = velocity[i] * seconds;
            length2 += travel[i] * travel[i];
            dot += delta[i] * travel[i];
        }
        if (!valid || !length2) continue;
        const auto fraction = std::clamp(dot / length2, 0.0, 1.0);
        double error2 = 0;
        for (int i = 0; i < 3; ++i) {
            const auto error = delta[i] - travel[i] * fraction;
            error2 += error * error;
        }
        if (error2 <= static_cast<double>(threshold) * threshold) return false;
    }
    return true;
}
}
