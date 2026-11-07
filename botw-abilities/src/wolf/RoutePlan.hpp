// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace botw_abilities::wolf {

enum class Route : std::uint8_t { Direct, Near, Far };

// DistanceRequestingPath on the original follow and food nodes. The rule that reads them was not
// found; beyond the distance he plans a route, inside it he goes straight while the line is open, as
// the node names and CheckLineReachable describe. The follow band was widened from the original's 17
// after wolf-compact-12: route churn near the boundary (164 starts, 16 completions) was the circling
// Clay saw, and straight running self-recovers.
inline constexpr float kFollowRouteDistance = 24, kFoodRouteDistance = 10;
// GoalDistanceTolerance on the original far path.
inline constexpr float kRouteArrival = 8;
// The original's battle moves always plan a path (WolfLink.baiprog in Clay's BotW dump). The approach
// plans until 9 units out (CloseEndDist) with a 6-unit goal tolerance; the rush to the bite plans to
// the enemy itself (GoalDistanceTolerance 0, IsAutoGearDownEnabled off). Changed in wolf-compact-17
// from straight running inside 16 units with an 8-unit tolerance, which ran him into walls and
// restarted near routes every few ticks short of the bite (wolf-compact-15 run 2).
inline constexpr float kBattleArrival = 9, kBattleTolerance = 6, kRushTolerance = 0;
// The enemy moves, so the rush updates its goal this often (ticks).
inline constexpr float kRushRetarget = 10;

// Decides how the wolf travels. A route the game refuses, or one that leaves him standing, sends
// him back to running straight for a while, so a missing route never strands him.
class RoutePlan {
public:
    static constexpr int kStrikes = 3;
    static constexpr float kLineTicks = 8, kFarRetarget = 60, kNearRetarget = 15;
    static constexpr float kOffTicks = 600, kStallTicks = 60;
    // A running far route keeps going until the goal is well inside the straight-run band, so the
    // wolf does not flip between planning and running at the boundary (wolf-compact-12 churn).
    static constexpr float kFarExitMargin = 6;

    void advance(float delta) {
        offFor_ = offFor_ > delta ? offFor_ - delta : 0;
        lineIn_ -= delta;
        retargetIn_ -= delta;
    }
    [[nodiscard]] bool off() const { return offFor_ > 0; }
    [[nodiscard]] Route choose(float distance, float routeDistance, bool always, Route current) const {
        if (off()) return Route::Direct;
        if (always) return distance > kRouteArrival ? Route::Far : Route::Direct;
        if (distance > (current == Route::Far ? routeDistance - kFarExitMargin : routeDistance))
            return Route::Far;
        return lineClear_ ? Route::Direct : Route::Near;
    }
    [[nodiscard]] bool lineDue() {
        if (lineIn_ > 0) return false;
        lineIn_ = kLineTicks;
        return true;
    }
    // One stray probe on broken ground flapped him between straight running and a near route every
    // few ticks; the verdict only flips after two consecutive probes agree (wolf-compact-12).
    void line(bool clear) {
        if (clear == lineClear_) {
            lineVotes_ = 0;
        } else if (++lineVotes_ >= 2) {
            lineClear_ = clear;
            lineVotes_ = 0;
        }
    }
    // every overrides the route's own retarget interval when positive.
    void started(Route route, float every = 0) {
        retargetIn_ = interval(route, every);
        stall_ = 0;
    }
    [[nodiscard]] bool retargetDue(Route route, float every = 0) {
        if (retargetIn_ > 0) return false;
        retargetIn_ = interval(route, every);
        return true;
    }
    // A running route that has not moved him for this long is treated as refused.
    [[nodiscard]] bool stalled(bool moved, float delta) {
        stall_ = moved ? 0 : stall_ + delta;
        return stall_ >= kStallTicks;
    }
    void progress() { strikes_ = 0; }
    // Returns true when this strike turned routes off.
    bool strike() {
        stall_ = 0;
        if (++strikes_ < kStrikes) return false;
        strikes_ = 0;
        offFor_ = kOffTicks;
        return true;
    }
private:
    [[nodiscard]] static constexpr float interval(Route route, float every) {
        return every > 0 ? every : route == Route::Near ? kNearRetarget : kFarRetarget;
    }
    float offFor_{}, lineIn_{}, retargetIn_{}, stall_{};
    int strikes_{}, lineVotes_{};
    bool lineClear_ = true;
};

}
