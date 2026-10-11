// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisRuntime.hpp"
#include "CryonisFeature.hpp"
#include "CryonisRuntimeState.hpp"
#include <lib.hpp>
namespace cryonis::runtime {
using namespace engine;
namespace {
// Require the whole pillar footprint to remain on the same water surface.
bool onOneSurface(const Target &proposed) {
    for (unsigned corner = 0; corner < 4; ++corner) {
        const auto point =
            footprintPoint(proposed.base, corner & 1 ? kWidth * .5f : -kWidth * .5f, 0,
                           corner & 2 ? kWidth * .5f : -kWidth * .5f, proposed.yaw);
        const auto water = sample(world(), point.x, point.z);
        if (!water.present || std::fabs(water.surface - proposed.base.y) > 0.1f)
            return false;
    }
    return true;
}
void accept(const Target &target) {
    auto &targeting = runtimeState.targeting;
    targeting.validated = target;
    targeting.validatedTick = runtimeState.controls.ticks;
    targeting.validatedRay = targeting.requestRay;
}
}
void select() {
    auto &targeting = runtimeState.targeting;
    const auto &pillars = runtimeState.gameplay.pillars;
    const auto ticks = runtimeState.controls.ticks;
    const auto generation = runtimeState.signals.generation.load();
    const auto ray = cameraRay(hooks());
    if (!ray.valid) {
        targeting.validated = {};
        targeting.preview = {};
        return;
    }
    Target proposed = waterTarget(ray, [](float x, float z) { return sample(world(), x, z); });
    if (proposed.valid) {
        proposed.yaw = faceLink(proposed.base, position(runtimeState.scene.player),
                                targeting.preview.yaw);
        proposed.valid = onOneSurface(proposed);
    }
    targeting.preview = proposed;
    target::Hit hit;
    if (targeting.cast.poll(hit, generation)) {
        targeting.validated = {};
        if (hit.filterValid) {
            const float waterDistance = distance(targeting.requested.base, targeting.requestRay.eye);
            const float solidDistance =
                hit.hit ? distance(hit.position, targeting.requestRay.eye) : 10000;
            if (hit.clearance && solidDistance + 0.1f >= waterDistance &&
                target::compatibleTarget(targeting.requested, proposed, targeting.requestRay, ray))
                accept(targeting.requested);
            for (unsigned i = 0; i < kPillarLimit; ++i)
                if (pillars[i].occupied && seen(world(), runtimeState.pillars.actors[i]) &&
                    hit.hit && target::hitOnPillar(hit.position, pillars[i]) &&
                    target::sameRay(targeting.requestRay, ray))
                    accept({pillars[i].base, {0, 1, 0}, true, int(i), pillars[i].yaw});
        }
    }
    if (ticks - targeting.validatedTick > 8 ||
        (targeting.validated.pillar < 0
             ? !target::compatibleTarget(targeting.validated, proposed, targeting.validatedRay, ray)
             : !target::sameRay(targeting.validatedRay, ray)))
        targeting.validated = {};
    // Show fresh water while querying; placement and accepted preview share the validated base.
    if (targeting.validated.valid)
        targeting.preview = targeting.validated;
    if (targeting.cast.busy() && ticks - targeting.requestTick > 12) {
        targeting.cast.cancel();
        targeting.validated = {};
    }
    if (targeting.cast.busy())
        return;
    const auto exclude = collisionGroup(runtimeState.scene.player);
    targeting.requestRay = ray;
    if (targeting.cast.begin(ray, position(runtimeState.scene.player), generation, exclude,
                             proposed)) {
        targeting.requested = proposed;
        targeting.requestTick = ticks;
    } else if (ticks % 120 == 0)
        Logging.Log("[cryonis] CAST_REFUSED group=%u enabled=%u", exclude.id,
                    unsigned(exclude.enabled));
}
}
