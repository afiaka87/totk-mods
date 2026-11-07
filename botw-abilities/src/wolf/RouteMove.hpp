// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CliffEscape.hpp"
#include "CloseFollow.hpp"
#include "PathFollow.hpp"
#include "RoutePlan.hpp"

namespace botw_abilities::wolf {

struct RouteRequest {
    totk::core::WorldPosition target;
    float keptDistance{}, delta{}, routeDistance{};
    int gearLimit = 4;  // Ceiling while running straight.
    int gearFloor{};  // Floor while running straight, so a sprinting player keeps him at pace.
    int routeGear{};  // Fixed gear on a far route; zero lets the corridor checks choose.
    const totk::core::WorldTransform* frame{};  // Set on the moves that have a cliff child.
    float leaderDistance{};
    PathGoal goal;
    float arrival = kRouteArrival;  // A chase runs straight only inside this.
    float retarget{};  // Ticks between goal updates on a running route; zero keeps the route's own.
    bool predict{}, routes{}, always{};
    // A chase always plans, and a route the game refuses is reported instead of run straight.
    bool chase{};
};

// AI-owner only. Runs straight or plans a route, moving between them as the original's children do.
class RouteMove {
public:
    // Failed means he has given up; running straight never reports it. With a frame a route that
    // gives up, or no ground ahead at speed, tries the cliff escape first, as the original does. A
    // chase reports every refused or abandoned route at once.
    [[nodiscard]] FollowStatus update(const ActorReference& actor, const ActorReference& leader,
        const totk::core::WorldPosition& position, const RouteRequest& request);
    void stop(const ActorReference& actor);
    // The straight-run gait currently playing, or -1 while a planned route or escape owns him.
    [[nodiscard]] int closeGear() const { return closeStarted_ ? close_.gear() : -1; }
    // For the log: 0 nothing moves him, 1 running straight, 2 a planned route, 3 the cliff escape.
    [[nodiscard]] unsigned owner() const { return escaping_ ? 3u : pathStarted_ ? 2u : closeStarted_ ? 1u : 0u; }
private:
    [[nodiscard]] FollowStatus direct(const ActorReference& actor, const ActorReference& leader,
        const RouteRequest& request);
    [[nodiscard]] FollowStatus planned(const ActorReference& actor,
        const totk::core::WorldPosition& position, const RouteRequest& request, Route route);
    // Running for a move that falls back to running straight, Failed for a chase.
    FollowStatus refuse(const ActorReference& actor, const RouteRequest& request, const char* reason);
    void enterEscape(const ActorReference& actor, const RouteRequest& request, const char* reason);
    [[nodiscard]] FollowStatus escape(const ActorReference& actor, const RouteRequest& request);
    void leavePath(const ActorReference& actor);
    void leaveClose(const ActorReference& actor);

    CloseFollow close_;
    PathFollow path_;
    CliffEscape escape_;
    RoutePlan plan_;
    totk::core::WorldPosition from_{};
    Route route_{};
    float tolerance_{};
    int gear_{};
    unsigned ticks_{}, retries_{}, refusals_{}, blockedAhead_{};
    float sinceEscape_{};
    std::uint8_t escapes_{};
    bool closeStarted_{}, pathStarted_{}, escaping_{};
};

}
