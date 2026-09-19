// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

namespace bivouac::feature {

// After a camp warp, lifts Link back onto the deck if he arrives before it spawns and falls past it.
constexpr float kArrivalRadiusMeters = 30.0f;
constexpr float kArrivalJumpMeters = 10.0f;
constexpr int kArrivalWaitTicks = 2400;
constexpr float kLiftRadiusMeters = 20.0f;
constexpr float kLiftBelowMinimumMeters = 0.7f;
constexpr int kMaximumLifts = 3;
constexpr int kWatchMinimumTicks = 200;
constexpr int kWatchAfterDeckTicks = 80;
constexpr int kWatchMaximumTicks = 1200;

enum class ArrivalPhase { Idle, AwaitingArrival, Watching };

enum class ArrivalAction {
    None,
    Arrived,
    DeckReady,
    Lift,
    EndedSettled,
    EndedDeckNeverReady,
    EndedNoArrival,
};

struct ArrivalWatch {
    ArrivalPhase phase = ArrivalPhase::Idle;
    int armedTick = 0;
    int arrivedTick = 0;
    int deckReadyTick = -1;
    int lifts = 0;
    bool havePrevious = false;
    bool jumpSeen = false;
    float previousX = 0.0f;
    float previousZ = 0.0f;
};

struct ArrivalInput {
    int tick;
    float linkX, linkY, linkZ;
    float anchorX, anchorY, anchorZ;
    bool deckReady;
};

inline void armArrival(ArrivalWatch& watch, int tick) {
    watch = ArrivalWatch{};
    watch.phase = ArrivalPhase::AwaitingArrival;
    watch.armedTick = tick;
}

inline ArrivalAction stepArrival(ArrivalWatch& watch, const ArrivalInput& in) {
    const float dx = in.linkX - in.anchorX;
    const float dz = in.linkZ - in.anchorZ;
    const float horizontalSquared = dx * dx + dz * dz;

    if (watch.phase == ArrivalPhase::AwaitingArrival) {
        // Warping to a camp Link is already next to: no jump will come, so count it as arrived.
        const bool alreadyThere = !watch.havePrevious
            && horizontalSquared <= kArrivalRadiusMeters * kArrivalRadiusMeters;
        if (alreadyThere) {
            watch.jumpSeen = true;
        }
        if (watch.havePrevious) {
            const float mx = in.linkX - watch.previousX;
            const float mz = in.linkZ - watch.previousZ;
            if (mx * mx + mz * mz >= kArrivalJumpMeters * kArrivalJumpMeters) {
                watch.jumpSeen = true;
            }
        }
        watch.havePrevious = true;
        watch.previousX = in.linkX;
        watch.previousZ = in.linkZ;
        if (watch.jumpSeen && horizontalSquared <= kArrivalRadiusMeters * kArrivalRadiusMeters) {
            watch.phase = ArrivalPhase::Watching;
            watch.arrivedTick = in.tick;
            return ArrivalAction::Arrived;
        }
        if (in.tick - watch.armedTick > kArrivalWaitTicks) {
            watch.phase = ArrivalPhase::Idle;
            return ArrivalAction::EndedNoArrival;
        }
        return ArrivalAction::None;
    }

    if (watch.phase != ArrivalPhase::Watching) {
        return ArrivalAction::None;
    }
    const int sinceArrival = in.tick - watch.arrivedTick;
    if (in.deckReady && watch.deckReadyTick < 0) {
        watch.deckReadyTick = in.tick;
        return ArrivalAction::DeckReady;
    }
    if (in.deckReady && watch.lifts < kMaximumLifts
        && horizontalSquared <= kLiftRadiusMeters * kLiftRadiusMeters
        && in.anchorY - in.linkY >= kLiftBelowMinimumMeters) {
        watch.lifts++;
        return ArrivalAction::Lift;
    }
    if (watch.deckReadyTick >= 0 && sinceArrival >= kWatchMinimumTicks
        && in.tick - watch.deckReadyTick >= kWatchAfterDeckTicks) {
        watch.phase = ArrivalPhase::Idle;
        return ArrivalAction::EndedSettled;
    }
    if (sinceArrival > kWatchMaximumTicks) {
        watch.phase = ArrivalPhase::Idle;
        return watch.deckReadyTick >= 0 ? ArrivalAction::EndedSettled
                                        : ArrivalAction::EndedDeckNeverReady;
    }
    return ArrivalAction::None;
}

} // namespace bivouac::feature
