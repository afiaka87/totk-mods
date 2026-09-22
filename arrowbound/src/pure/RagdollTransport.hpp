// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include "Vec3.hpp"

namespace arrowbound::pure {
struct RagdollBodyPose {
    std::array<float, 12> matrix{};
    Vec3 velocity{};
    Vec3 position() const { return {matrix[3], matrix[7], matrix[11]}; }
};

inline Vec3 transformPoint(const float* matrix, Vec3 point) {
    return {matrix[0]*point.x + matrix[1]*point.y + matrix[2]*point.z + matrix[3],
            matrix[4]*point.x + matrix[5]*point.y + matrix[6]*point.z + matrix[7],
            matrix[8]*point.x + matrix[9]*point.y + matrix[10]*point.z + matrix[11]};
}

enum class TransportResult { Native, Fast, Invalid };
struct RagdollTransport {
    // Latch only when the ragdoll falls substantially behind its animation target.
    static constexpr float kEngageDistance = 8.0f;
    // Native general velocity ceiling (1.2.1 clampVelocity), not an arrow speed.
    static constexpr float kSpeedCeiling = 5000.0f;
    bool engaged{};
    float gap{};

    TransportResult prepare(std::span<const RagdollBodyPose> bodies, std::size_t root, Vec3 target) {
        if (root >= bodies.size() || !finite3(target)) return TransportResult::Invalid;
        for (const auto& body : bodies) {
            if (!finite3(body.velocity)) return TransportResult::Invalid;
            for (float value : body.matrix)
                if (!std::isfinite(value)) return TransportResult::Invalid;
        }
        gap = distance(target, bodies[root].position());
        if (!std::isfinite(gap)) return TransportResult::Invalid;
        if (!engaged && gap < kEngageDistance) return TransportResult::Native;
        engaged = true;
        return TransportResult::Fast;
    }
};

// The address is an identity only. Callers supply the current native motion value.
struct SpeedLimitLease {
    std::uintptr_t motion{};
    float original{}, imposed{};
    bool owned{};

    float update(std::uintptr_t currentMotion, float current, bool active) {
        if (motion != currentMotion || (owned && current != imposed)) {
            *this = {};
            motion = currentMotion;
        }
        if (!active) {
            const float restored = owned ? original : current;
            owned = false;
            return restored;
        }
        if (current >= RagdollTransport::kSpeedCeiling) return current;
        if (!owned) original = current;
        imposed = RagdollTransport::kSpeedCeiling;
        owned = true;
        return imposed;
    }
};

inline Vec3 boundedFlightVelocity(Vec3 input) {
    const float speed = length(input);
    return speed > RagdollTransport::kSpeedCeiling
        ? mul(input, RagdollTransport::kSpeedCeiling / speed) : input;
}
}
