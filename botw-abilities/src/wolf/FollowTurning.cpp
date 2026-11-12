// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "FollowTurning.hpp"
#include "WorldMath.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace botw_abilities::wolf {
namespace {
using Vector = totk::core::WorldPosition;
struct SinCos { float sine, cosine; };

SinCos circleAt(std::uint32_t index, CircleTable table) {
    const auto& sample = table[index >> 24];
    const float fraction = static_cast<float>(index & 0xFFFFFF) * 0x1p-24f;
    return {sample.sine + sample.sineDelta * fraction, sample.cosine + sample.cosineDelta * fraction};
}

SinCos circleAtRadians(float angle, CircleTable table) {
    return circleAt(static_cast<std::uint32_t>(static_cast<std::int64_t>(angle * 683565248.0f)), table);
}
}

std::optional<KeptDistanceTurn> keptDistanceTurn(Vector facing, Vector target, float distance,
    float keptDistance, float minimumRadius, float lookAhead, bool predictTravel, CircleTable table) {
    if (distance == 0) return std::nullopt;
    const auto forward = horizontalUnit(facing);
    const float dot = (forward.x * target.x + forward.y * target.y) + forward.z * target.z;
    Vector side{forward.x - target.x * dot, forward.y - target.y * dot, forward.z - target.z * dot};
    const float length = std::sqrt(side.z * side.z + (side.x * side.x + side.y * side.y));
    if (length > 0) {
        const float inverse = 1.0f / length;
        side = {inverse * side.x, inverse * side.y, inverse * side.z};
    } else if (length == 0) {
        const float y = target.y * 0.0f;
        side = {y - target.z, target.z * 0.0f - target.x * 0.0f, target.x - y};
    }
    const float turn = std::clamp((keptDistance * 0.75f) / distance, 0.0f, 2.0f);
    const auto direction = circleAt(static_cast<std::uint32_t>(turn * 0x1p30f), table);
    KeptDistanceTurn result{{side.x * direction.sine + target.x * direction.cosine,
                            side.y * direction.sine + target.y * direction.cosine,
                            side.z * direction.sine + direction.cosine * target.z}, {}};
    if (predictTravel) {
        const float radius = keptDistance <= minimumRadius ? minimumRadius : keptDistance;
        const auto arc = circleAtRadians(lookAhead / radius, table);
        const float forwardDistance = distance - radius * arc.cosine;
        const float sideDistance = radius * arc.sine;
        result.travel = Vector{side.x * sideDistance + target.x * forwardDistance,
                               side.y * sideDistance + target.y * forwardDistance,
                               side.z * sideDistance + forwardDistance * target.z};
    }
    return result;
}

ApproachTurn approachTurn(Vector facing, Vector target, float distance, float minimumRadius,
    float exitRadius, float lookAhead, bool predictTravel, CircleTable table) {
    const auto forward = horizontalUnit(facing);
    const float sign = forward.x * target.z - forward.z * target.x < 0 ? -1.0f : 1.0f;
    const float alignment = forward.z * target.z + (forward.x * target.x + forward.y * target.y);
    const float perpendicularSquared = 1.0f - alignment * alignment;
    const float inverseSine = perpendicularSquared > 0 ? 1.0f / std::sqrt(perpendicularSquared)
                                                      : std::numeric_limits<float>::infinity();
    const float radius = perpendicularSquared > 0 ? (distance * 0.5f) * inverseSine : inverseSine;
    const bool inside = radius < minimumRadius;
    ApproachTurn result{inside ? (sign >= 0 ? 1 : 3) : (sign >= 0 ? 2 : 0), alignment, {}};
    if (!predictTravel) return result;

    if (perpendicularSquared > 0 && inside) {
        const float chord = distance / inverseSine;
        const float advance = alignment * distance + std::sqrt(std::fmax(chord * ((exitRadius + exitRadius) - chord), 0.0f));
        const Vector center{(forward.z * -sign) * exitRadius + forward.x * advance,
                            exitRadius * 0.0f + forward.y * advance,
                            (forward.x * sign) * exitRadius + forward.z * advance};
        const float length = std::sqrt(center.z * center.z + (center.x * center.x + center.y * center.y));
        Vector unit{};
        if (length > 0) {
            const float inverse = 1.0f / length;
            unit = {inverse * center.x, inverse * center.y, inverse * center.z};
        }
        const auto arc = circleAtRadians(lookAhead / exitRadius, table);
        const float along = arc.cosine * exitRadius, across = arc.sine * exitRadius;
        result.travel = Vector{(sign * unit.z) * across + (center.x - unit.x * along),
                               across * 0.0f + (center.y - unit.y * along),
                               (unit.x * -sign) * across + (center.z - unit.z * along)};
        return result;
    }
    if (std::fabs(radius) <= std::numeric_limits<float>::max()) {
        constexpr float tau = 6.2831854820251465f, inverseTau = 0.15915493667125702f;
        const float angle = std::acos(std::clamp(alignment, -1.0f, 1.0f));
        const float twice = angle + angle;
        float remaining = twice - std::floor(twice * inverseTau) * tau;
        if (remaining >= tau) remaining = 0;
        const float sweep = lookAhead / radius;
        if (remaining > sweep) {
            const auto arc = circleAtRadians(sweep, table);
            const float along = radius * arc.sine, across = radius * (1.0f - arc.cosine);
            result.travel = Vector{forward.x * along - (forward.z * sign) * across,
                                   forward.y * along + across * 0.0f,
                                   forward.z * along + (forward.x * sign) * across};
            return result;
        }
    }
    result.travel = Vector{target.x * lookAhead, target.y * lookAhead, target.z * lookAhead};
    return result;
}

FollowSteering steerCloseFollow(const FollowSteeringInput& input, CircleTable table) {
    Vector direction{input.target.x - input.position.x, 0, input.target.z - input.position.z};
    const float distance = std::sqrt((direction.x * direction.x + 0.0f) + direction.z * direction.z);
    if (distance > 0) {
        const float inverse = 1.0f / distance;
        direction = {inverse * direction.x, inverse * 0.0f, inverse * direction.z};
    }
    const float radius = (input.speed / input.angularSpeed) * 1.3f;
    const float lookAhead = std::fmax(input.speed * 3.0f, 8.0f);
    FollowSteering result{{}, {input.target.x, input.position.y, input.target.z}, input.insideTurn};
    std::optional<Vector> travel;
    if (input.keptDistance >= 0x1p-23f) {
        const auto turn = keptDistanceTurn(input.facing, direction, distance,
            input.keptDistance, radius, lookAhead, input.predictTravel, table);
        direction = turn ? turn->direction : input.facing;
        if (turn) travel = turn->travel;
    } else {
        const float exitRadius = radius + radius;
        const auto turn = approachTurn(input.facing, direction, distance,
            input.insideTurn ? exitRadius : radius, exitRadius, lookAhead, input.predictTravel, table);
        result.insideTurn = (turn.category | 2) == 3 && turn.alignment < 0x1.f837b4p-1f;
        if (result.insideTurn) direction = input.facing;
        travel = turn.travel;
    }
    if (travel) result.target = {input.position.x + travel->x,
                                input.position.y + travel->y, input.position.z + travel->z};
    result.velocity = {input.speed * direction.x, input.speed * direction.y, input.speed * direction.z};
    return result;
}

}
