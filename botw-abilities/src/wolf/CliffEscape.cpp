// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CliffEscape.hpp"
#include "NativeNavigation.hpp"
#include <lib.hpp>
#include <limits>

namespace botw_abilities::wolf {
namespace {
constexpr float kFootingRange = 3, kGroundRange = 10;

// With footing nearby the original fans out for the best direction; without it he makes for the
// nearest ground. The original also filters by surface material, which is not mapped to TotK.
CliffSearchResult searchEscape(const ActorReference& actor, const totk::core::WorldTransform& transform,
                               const totk::core::WorldPosition& goal) {
    const auto& position = transform.position;
    const auto& rotation = transform.rotation.values;
    if (nearestNavigationSurface(position, kFootingRange)) {
        if (const auto open = findCliffEscape(actor, {rotation[2], rotation[5], rotation[8]}, goal))
            return {*open, false};
    } else if (const auto ground = nearestNavigationSurface(position, kGroundRange)) {
        return {ground->position, false};
    }
    Logging.Log("[wolf-link] CLIFF_SEARCH_EMPTY");
    constexpr float none = std::numeric_limits<float>::quiet_NaN();
    return {{none, none, none}, true};
}
}

FollowStatus CliffEscape::begin(const ActorReference& actor, const totk::core::WorldTransform& transform,
    const totk::core::WorldPosition& goal, float delta) {
    return controller_.begin(actor, transform, delta,
        [&](const totk::core::WorldPosition&) { return searchEscape(actor, transform, goal); });
}

FollowStatus CliffEscape::update(const ActorReference& actor, const totk::core::WorldTransform& transform,
    const totk::core::WorldPosition& goal, float delta) {
    return controller_.update(actor, transform, delta, true,
        [&](const totk::core::WorldPosition&) { return searchEscape(actor, transform, goal); });
}

void CliffEscape::stop(const ActorReference& actor) { controller_.stop(actor); }

}
