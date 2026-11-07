// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/core/Units.hpp"
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>

namespace botw_abilities::wolf {

// One of the original's SafeMoveAroundTarget children in the battle band: the circle starts at startRange
// and tightens toward endRange; tries is UpdateNumCalc.
struct CircleRanges {
    float startRange{}, endRange{};
    int tries{};
};
// 通常行動 picks the inner or the outer circle by distance; 離脱 backs off to a wider one.
inline constexpr CircleRanges kInnerCircle{10.5f, 6, 10}, kOuterCircle{15, 6, 10}, kLeaveCircle{12, 12, 5};

// The original's SafeMoveAroundTarget. Every UpdateTargetPosTime ticks it aims a few units ahead along a
// circle round the enemy, in the direction the wolf already faces, and takes the first aim with ground
// under it. A timed pause aims the other way, so he stops and turns back now and then.
class CircleMove {
public:
    static constexpr float kTurnBase = 100, kTurnSpread = 60, kStopBase = 20, kStopSpread = 5;
    static constexpr float kUpdate = 2, kOffsetDegrees = 40, kTryDegrees = 2, kRangeRate = -0.02f;
    // The aim is max(speed per tick x UpdateTargetPosTime, MinOffsetLength) ahead; at any running gait
    // the first term is under a unit, so the minimum always wins.
    static constexpr float kStep = 5;
    // The circle stops tightening once it is within 0.95 of EndRange.
    static constexpr float kEndSlack = 0.95f;

    // constexpr keeps the owning follow state constant-initialized, with no load-time constructor.
    constexpr explicit CircleMove(std::uint32_t seed = 1) : random_(seed | 1) {}

    void begin(const CircleRanges& ranges) {
        ranges_ = ranges;
        radius_ = ranges.startRange;
        turn_ = between(kTurnBase, kTurnSpread);
        stop_ = update_ = 0;
        point_.reset();
    }

    // ground(point) returns the ground under an aim, or nothing. Returns nothing once every aim failed,
    // where the original ends the child.
    template<class Ground>
    std::optional<totk::core::WorldPosition> update(const totk::core::WorldPosition& wolf,
        const totk::core::WorldPosition& forward, const totk::core::WorldPosition& enemy, float delta,
        Ground&& ground) {
        update_ -= delta;
        if (turn_ > 0) {
            turn_ -= delta;
            if (turn_ <= 0) stop_ = between(kStopBase, kStopSpread);
        }
        if (stop_ > 0) {
            stop_ -= delta;
            if (stop_ <= 0) turn_ = between(kTurnBase, kTurnSpread);
        }
        if (radius_ > ranges_.endRange * kEndSlack) radius_ += kRangeRate * delta;
        if (update_ <= 0) {
            point_ = pick(wolf, forward, enemy, ground);
            update_ = kUpdate;
        }
        return point_;
    }

    [[nodiscard]] float radius() const { return radius_; }
    [[nodiscard]] bool pausing() const { return stop_ > 0; }

private:
    template<class Ground>
    std::optional<totk::core::WorldPosition> pick(const totk::core::WorldPosition& wolf,
        const totk::core::WorldPosition& forward, const totk::core::WorldPosition& enemy, Ground& ground) {
        float toX = enemy.x - wolf.x, toZ = enemy.z - wolf.z;
        if (const float length = std::sqrt(toX * toX + toZ * toZ); length > 0) {
            toX /= length;
            toZ /= length;
        }
        // Which way round the wolf is already heading; the remembered side updates only near a pause.
        const float heading = forward.z * toX - forward.x * toZ;
        if (turn_ <= kUpdate) side_ = heading;
        const float side = heading < 0 ? -1.0f : 1.0f;
        const auto aim = [&](float degrees) {
            const float angle = degrees * (std::numbers::pi_v<float> / 180);
            const float s = std::sin(angle), c = std::cos(angle);
            float x = c * -toX + s * -toZ, z = c * -toZ - s * -toX;
            if (const float length = std::sqrt(x * x + z * z); length > 0) {
                x /= length;
                z /= length;
            }
            float stepX = enemy.x + radius_ * x - wolf.x, stepZ = enemy.z + radius_ * z - wolf.z;
            if (const float length = std::sqrt(stepX * stepX + stepZ * stepZ); length > 0) {
                stepX /= length;
                stepZ /= length;
            }
            return totk::core::WorldPosition{wolf.x + stepX * kStep, wolf.y, wolf.z + stepZ * kStep};
        };
        const bool pausing = stop_ > 0;
        if (!pausing) {
            for (int i = 0; i < ranges_.tries; ++i) {
                if (auto found = ground(aim(side * (kOffsetDegrees + kTryDegrees * float(i))))) return found;
            }
        }
        // The other way round. As in the original every try runs and the last one stands: its ground if
        // it has any, else the bare aim, as long as some aim found ground or the wolf is pausing.
        bool usable = pausing;
        totk::core::WorldPosition last{};
        for (int i = 0; i < ranges_.tries; ++i) {
            const float degrees = -(kOffsetDegrees + kTryDegrees * float(i));
            const auto point = aim(usable ? degrees * (side_ < 0 ? -1.0f : 1.0f) : side * degrees);
            if (auto found = ground(point)) {
                usable = true;
                last = *found;
            } else {
                last = point;
            }
        }
        if (!usable) return std::nullopt;
        return last;
    }

    [[nodiscard]] float between(float base, float spread) {
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        return base + spread * float(random_ >> 8) * (1.0f / 16777216.0f);
    }

    CircleRanges ranges_{};
    std::optional<totk::core::WorldPosition> point_;
    float radius_{}, turn_{}, stop_{}, update_{}, side_{};
    std::uint32_t random_;
};

}
