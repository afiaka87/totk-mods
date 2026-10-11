// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisRuntime.hpp"
#include "CryonisFeature.hpp"
#include "CryonisRuntimeState.hpp"
#include "CryonisAudio.hpp"
#include <lib.hpp>
namespace cryonis::runtime {
using namespace engine;
namespace {
void clearPending() {
    runtimeState.pillars.pending = {};
    runtimeState.pillars.capturedRiders = {};
}
bool owned(Handle candidate) {
    for (const auto h : runtimeState.pillars.actors)
        if (h.address == candidate.address &&
            h.capturedNamePointer == candidate.capturedNamePointer)
            return true;
    return false;
}
// Bind the pending pillar to its new actor once the body exists; until then, retry next tick.
void adopt(Handle candidate) {
    auto &pillars = runtimeState.pillars;
    auto &gameplay = runtimeState.gameplay;
    const auto b = body(candidate);
    if (!totk::engine::isPlausibleAddress(b) || !shadowOnly(candidate))
        return;
    const auto r = pillars.pending.reservation;
    const auto &old = gameplay.pillars[r.slot];
    if ((old.occupied ? old.id : 0) != r.remove) {
        Logging.Log("[cryonis] STALE_SPAWN slot=%u", r.slot);
        clearPending();
        return;
    }
    if (r.remove)
        erase(r.slot, "oldest replacement");
    // erase updates the old slot; reserve's old identity was already checked.
    auto ready = r;
    ready.remove = 0;
    if (gameplay.commit(ready, pillars.pending.target)) {
        pillars.actors[r.slot] = candidate;
        pillars.riders[r.slot] = pillars.capturedRiders;
        pillars.endPlayed[r.slot] = false;
        audio::play(audio::Cue::Making);
        fixBody(b);
        Logging.Log("[cryonis] SPAWN_READY slot=%u actor=%llx id=%llu", r.slot,
                    static_cast<unsigned long long>(candidate.address), gameplay.pillars[r.slot].id);
    } else {
        deleteActor(candidate);
        Logging.Log("[cryonis] COMMIT_REFUSED slot=%u", r.slot);
    }
    clearPending();
}
void advance(unsigned slot) {
    auto &pillars = runtimeState.pillars;
    const auto &pillar = runtimeState.gameplay.pillars[slot];
    if (!seen(world(), pillars.actors[slot])) {
        Logging.Log("[cryonis] PILLAR_LOST slot=%u", slot);
        runtimeState.gameplay.remove(pillar.id);
        pillars.actors[slot] = {};
        pillars.riders[slot] = {};
        return;
    }
    // Full-width collision rises from below the surface; final box is exact.
    const auto frame = pillar.growth.frame;
    const bool finished = pillar.growth.finished();
    move(pillars.actors[slot], pillar.base, frame, pillar.yaw);
    if (pillars.riders[slot].count)
        liftRiders(world(), pillars.riders[slot], pillar.base, frame, finished, pillar.yaw);
    if (finished && !pillars.endPlayed[slot]) {
        audio::play(audio::Cue::End);
        pillars.endPlayed[slot] = true;
    }
}
}
void erase(unsigned slot, const char *why) {
    if (slot >= kPillarLimit)
        return;
    auto &pillars = runtimeState.pillars;
    const auto &pillar = runtimeState.gameplay.pillars[slot];
    if (seen(world(), pillars.actors[slot]))
        deleteActor(pillars.actors[slot]);
    runtimeState.presentation.broken = pillar.base;
    runtimeState.presentation.breakAge = 0;
    audio::play(audio::Cue::Crush);
    pillars.endPlayed[slot] = false;
    Logging.Log("[cryonis] DESTROY slot=%u id=%llu reason=%s", slot, pillar.id, why);
    runtimeState.gameplay.remove(pillar.id);
    pillars.actors[slot] = {};
    pillars.riders[slot] = {};
}

void requestSpawn() {
    auto &pillars = runtimeState.pillars;
    const auto target = runtimeState.targeting.validated;
    const auto r = runtimeState.gameplay.reserve(target);
    if (!r.valid || pillars.pending.active) {
        Logging.Log("[cryonis] PLACE_REFUSED valid=%u cooldown=%f pending=%u",
                    unsigned(target.valid), double(runtimeState.gameplay.cooldown),
                    unsigned(pillars.pending.active));
        return;
    }
    if (!spawnPillar(target))
        return;
    audio::play(audio::Cue::Start);
    pillars.pending = {target, r, runtimeState.signals.generation.load(), 0, true};
    pillars.capturedRiders = captureRiders(world(), target.base, target.yaw);
    liftRiders(world(), pillars.capturedRiders, target.base, 0, false, target.yaw);
    runtimeState.gameplay.aiming = false;
    clearTarget(false);
}

void updatePillars(float frames) {
    auto &pending = runtimeState.pillars.pending;
    if (pending.active)
        pending.age += frames;
    const auto &roster = runtimeState.scene.roster;
    for (unsigned index = 0; index < roster.candidateCount; ++index) {
        const auto candidate = roster.candidates[index];
        if (owned(candidate))
            continue;
        const float d = distance(position(candidate), collisionBase(pending.target.base, 0));
        if (pending.active && pending.generation == runtimeState.signals.generation.load() &&
            d < 0.2f) {
            adopt(candidate);
        } else if (!pending.active || pending.age > 150) {
            deleteActor(candidate);
            Logging.Log("[cryonis] ORPHAN_DELETE actor=%llx distance=%f",
                        static_cast<unsigned long long>(candidate.address), double(d));
        }
    }
    if (pending.active && pending.age > 150) {
        Logging.Log("[cryonis] SPAWN_TIMEOUT age=%f", double(pending.age));
        clearPending();
    }
    if (pending.active)
        liftRiders(world(), runtimeState.pillars.capturedRiders, pending.target.base, 0, false,
                   pending.target.yaw);
    for (unsigned slot = 0; slot < kPillarLimit; ++slot)
        if (runtimeState.gameplay.pillars[slot].occupied)
            advance(slot);
}
}
