// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "RouteMove.hpp"
#include "WorldMath.hpp"
#include "NativeNavigation.hpp"
#include <lib.hpp>
#include <algorithm>
#include <cmath>

namespace botw_abilities::wolf {
namespace {
constexpr float kMovedDistance = 1;
constexpr unsigned kLoggedRefusals = 12;
// The original counts escapes from the moment it enters follow, which this has no single moment
// for, so a quiet stretch stands in.
constexpr float kEscapeForget = 600;
}

FollowStatus RouteMove::update(const ActorReference& actor, const ActorReference& leader,
    const totk::core::WorldPosition& position, const RouteRequest& request) {
    plan_.advance(request.delta);
    if (escaping_) {
        if (request.frame) return escape(actor, request);
        escape_.stop(actor);
        escaping_ = false;
    }
    sinceEscape_ += request.delta;
    if (sinceEscape_ >= kEscapeForget) escapes_ = 0;
    if (!request.routes) return direct(actor, leader, request);
    const float distance = flatDistance(request.target, position);
    if (request.chase) {
        // A running chase route keeps going until it is inside its own goal tolerance.
        const float inside = pathStarted_ ? std::min(request.arrival, request.goal.tolerance) : request.arrival;
        return distance > inside ? planned(actor, position, request, Route::Far) : direct(actor, leader, request);
    }
    if (!request.always && distance <= request.routeDistance && !plan_.off() && plan_.lineDue())
        plan_.line(probeFollowPoint(actor, request.target) != Reachability::Blocked);
    const auto route = plan_.choose(distance, request.routeDistance, request.always,
                                    pathStarted_ ? route_ : Route::Direct);
    if (route == Route::Direct) return direct(actor, leader, request);
    return planned(actor, position, request, route);
}

FollowStatus RouteMove::direct(const ActorReference& actor, const ActorReference& leader,
    const RouteRequest& request) {
    leavePath(actor);
    if (!closeStarted_) {
        if (retries_ && ++ticks_ % 60 != 0) return FollowStatus::Running;
        closeStarted_ = close_.begin(actor);
        if (!closeStarted_) {
            if (++retries_ <= 8) Logging.Log("[wolf-link] FOLLOW_BEGIN_RETRY attempt=%u", retries_);
            return FollowStatus::Running;
        }
    }
    if (close_.update(actor, leader, request.target, request.keptDistance, request.predict,
                      request.gearLimit, request.gearFloor) == FollowStatus::Failed) {
        closeStarted_ = false;
        if (++retries_ <= 8) Logging.Log("[wolf-link] FOLLOW_UPDATE_RETRY attempt=%u", retries_);
        return FollowStatus::Running;
    }
    // Rough ground gives single-tick false blocks; the escape (and its wander) only starts once the
    // step ahead has read blocked for several ticks in a row (wolf-compact-12: 21 escape entries).
    if (request.frame && close_.gear() > 1 &&
        probeFollowStep(actor, request.frame->rotation) == Reachability::Blocked) {
        if (++blockedAhead_ >= 5) {
            blockedAhead_ = 0;
            enterEscape(actor, request, "ahead");
        }
    } else {
        blockedAhead_ = 0;
    }
    return FollowStatus::Running;
}

FollowStatus RouteMove::planned(const ActorReference& actor, const totk::core::WorldPosition& position,
    const RouteRequest& request, Route route) {
    leaveClose(actor);
    const int gear = route == Route::Far ? request.routeGear : 0;
    if (!pathStarted_ || route != route_ || gear != gear_ || request.goal.tolerance != tolerance_) {
        leavePath(actor);
        const bool started = gear ? path_.beginFixed(actor, gear, request.target, request.goal)
            : path_.begin(actor, route == Route::Near ? FollowRange::Near : FollowRange::Far, request.target,
                          request.goal);
        if (!started) return refuse(actor, request, "begin");
        pathStarted_ = true;
        route_ = route;
        gear_ = gear;
        tolerance_ = request.goal.tolerance;
        from_ = position;
        plan_.started(route, request.retarget);
        Logging.Log("[wolf-link] ROUTE_START route=%u gear=%d distance=%.1f tolerance=%.0f chase=%u", unsigned(route),
            gear, double(flatDistance(request.target, position)), double(tolerance_), unsigned(request.chase));
    } else if (plan_.retargetDue(route, request.retarget) && !path_.retarget(actor, request.target)) {
        return refuse(actor, request, "retarget");
    }
    const auto status = path_.update(actor, request.delta).status;
    const bool moved = flatDistance(from_, position) >= kMovedDistance;
    if (moved) {
        from_ = position;
        plan_.progress();
    }
    if (status == FollowStatus::Failed) {
        Logging.Log("[wolf-link] ROUTE_FAILED route=%u moved=%u", unsigned(route), unsigned(moved));
        static_cast<void>(refuse(actor, request, "failed"));
        if (request.chase || !request.frame) return FollowStatus::Failed;
        enterEscape(actor, request, "route");
        return FollowStatus::Running;
    }
    if (status == FollowStatus::Succeeded) {
        Logging.Log("[wolf-link] ROUTE_DONE route=%u", unsigned(route));
        leavePath(actor);
        return FollowStatus::Succeeded;
    }
    if (plan_.stalled(moved, request.delta)) return refuse(actor, request, "stalled");
    return FollowStatus::Running;
}

FollowStatus RouteMove::refuse(const ActorReference& actor, const RouteRequest& request, const char* reason) {
    leavePath(actor);
    // A chase never turns routes off: the companion waits and plans again, as the original does.
    const bool off = !request.chase && plan_.strike();
    if (++refusals_ <= kLoggedRefusals || off || request.chase) {
        Logging.Log("[wolf-link] ROUTE_REFUSED reason=%s count=%u running-straight=%u chase=%u", reason, refusals_,
            unsigned(off), unsigned(request.chase));
    }
    return request.chase ? FollowStatus::Failed : FollowStatus::Running;
}

void RouteMove::enterEscape(const ActorReference& actor, const RouteRequest& request, const char* reason) {
    leavePath(actor);
    leaveClose(actor);
    Logging.Log("[wolf-link] CLIFF_ESCAPE_START reason=%s count=%u", reason, unsigned(escapes_));
    static_cast<void>(escape_.begin(actor, *request.frame, request.target, request.delta));
    escaping_ = true;
}

FollowStatus RouteMove::escape(const ActorReference& actor, const RouteRequest& request) {
    const auto status = escape_.update(actor, *request.frame, request.target, request.delta);
    if (status == FollowStatus::Running) return status;
    escape_.stop(actor);
    escaping_ = false;
    sinceEscape_ = 0;
    const auto count = countCliffEscape(escapes_, status == FollowStatus::Failed,
                                        request.leaderDistance * request.leaderDistance);
    escapes_ = count.attempts;
    Logging.Log("[wolf-link] CLIFF_ESCAPE_DONE worked=%u count=%u gave-up=%u",
        unsigned(status == FollowStatus::Succeeded), unsigned(escapes_), unsigned(count.gaveUp));
    // The wandering child is an empty action that ends follow at once, and the next entry counts
    // from zero.
    if (count.wander) escapes_ = 0;
    return count.gaveUp ? FollowStatus::Failed : FollowStatus::Running;
}

void RouteMove::leavePath(const ActorReference& actor) {
    if (pathStarted_) path_.stop(actor);
    pathStarted_ = false;
}

void RouteMove::leaveClose(const ActorReference& actor) {
    if (closeStarted_) close_.stop(actor);
    closeStarted_ = false;
}

void RouteMove::stop(const ActorReference& actor) {
    if (escaping_) escape_.stop(actor);
    escaping_ = false;
    escapes_ = 0;
    blockedAhead_ = 0;
    leavePath(actor);
    leaveClose(actor);
}

}
