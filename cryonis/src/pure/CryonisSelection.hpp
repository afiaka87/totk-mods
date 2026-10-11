// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisCore.hpp"
#include "CryonisGrowth.hpp"
namespace cryonis::target {
inline bool hitOnPillar(Vec3 hit, const Pillar &pillar) {
    const auto local = rotateYaw(sub(hit, pillar.base), -pillar.yaw);
    return pillar.occupied && finite(hit) && finite(pillar.base) &&
           std::fabs(local.x) <= kWidth * .5f + .05f && std::fabs(local.z) <= kWidth * .5f + .05f &&
           hit.y >= pillar.base.y - kSettledSubmerge - .1f &&
           hit.y <= pillar.base.y + emergenceHeight(pillar.growth.frame) + .1f;
}
inline bool selected(unsigned slot, Target target, bool aiming) {
    return aiming && target.valid && target.pillar == int(slot);
}
inline bool sameRay(Ray a, Ray b) {
    return a.valid && b.valid && distance(a.eye, b.eye) < .15f && dot(a.direction, b.direction) > .9996f;
}
inline bool compatibleTarget(Target tested, Target current, Ray oldRay, Ray newRay) {
    if (!tested.valid || !current.valid || tested.pillar != current.pillar || !oldRay.valid ||
        !newRay.valid)
        return false;
    const Vec3 shift = sub(tested.base, current.base);
    return length(shift) <= .3f && std::fabs(shift.y) <= .05f && distance(oldRay.eye, newRay.eye) <= 1.0f &&
           dot(oldRay.direction, newRay.direction) > .9996f && std::cos(tested.yaw - current.yaw) > .9996f;
}
}
