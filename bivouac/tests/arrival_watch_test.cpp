// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "feature/ArrivalWatch.hpp"

#include <cstdio>

namespace {

int gFailures = 0;

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                              \
            ++gFailures;                                                                            \
        }                                                                                           \
    } while (false)

using namespace bivouac::feature;

ArrivalInput at(int tick, float x, float y, float z, bool deckReady) {
    return {tick, x, y, z, 1000.0f, 200.0f, 1000.0f, deckReady};
}

// Link stays at the old spot through loading, then appears at the camp.
void testLoadingScreenDoesNotEndTheWatch() {
    ArrivalWatch watch;
    armArrival(watch, 100);
    for (int tick = 101; tick < 800; ++tick) {
        CHECK(stepArrival(watch, at(tick, 0.0f, 50.0f, 0.0f, false)) == ArrivalAction::None);
    }
    CHECK(stepArrival(watch, at(800, 1000.0f, 202.0f, 1000.0f, false)) == ArrivalAction::Arrived);
}

void testFallPastDeckIsLifted() {
    ArrivalWatch watch;
    armArrival(watch, 0);
    stepArrival(watch, at(1, 0.0f, 50.0f, 0.0f, false));
    CHECK(stepArrival(watch, at(2, 1000.0f, 202.0f, 1000.0f, false)) == ArrivalAction::Arrived);
    CHECK(stepArrival(watch, at(3, 1000.0f, 150.0f, 1000.0f, false)) == ArrivalAction::None);
    CHECK(stepArrival(watch, at(40, 1000.0f, 90.0f, 1000.0f, true)) == ArrivalAction::DeckReady);
    CHECK(stepArrival(watch, at(41, 1000.0f, 90.0f, 1000.0f, true)) == ArrivalAction::Lift);
    CHECK(stepArrival(watch, at(42, 1000.0f, 200.15f, 1000.0f, true)) == ArrivalAction::None);
    CHECK(stepArrival(watch, at(300, 1000.0f, 200.15f, 1000.0f, true)) == ArrivalAction::EndedSettled);
    CHECK(watch.phase == ArrivalPhase::Idle);
}

void testLandingOnDeckNeedsNoLift() {
    ArrivalWatch watch;
    armArrival(watch, 0);
    stepArrival(watch, at(1, 0.0f, 50.0f, 0.0f, false));
    stepArrival(watch, at(2, 1000.0f, 202.0f, 1000.0f, false));
    CHECK(stepArrival(watch, at(5, 1000.0f, 201.0f, 1000.0f, true)) == ArrivalAction::DeckReady);
    for (int tick = 6; tick < 202; ++tick) {
        CHECK(stepArrival(watch, at(tick, 1000.0f, 200.1f, 1000.0f, true)) == ArrivalAction::None);
    }
    CHECK(stepArrival(watch, at(202, 1000.0f, 200.1f, 1000.0f, true)) == ArrivalAction::EndedSettled);
    CHECK(watch.lifts == 0);
}

void testLiftsAreCapped() {
    ArrivalWatch watch;
    armArrival(watch, 0);
    stepArrival(watch, at(1, 0.0f, 50.0f, 0.0f, false));
    stepArrival(watch, at(2, 1000.0f, 202.0f, 1000.0f, false));
    stepArrival(watch, at(3, 1000.0f, 100.0f, 1000.0f, true));
    int lifts = 0;
    for (int tick = 4; tick < 60; ++tick) {
        if (stepArrival(watch, at(tick, 1000.0f, 100.0f, 1000.0f, true)) == ArrivalAction::Lift) {
            ++lifts;
        }
    }
    CHECK(lifts == kMaximumLifts);
}

void testFarSidewaysIsNotLifted() {
    ArrivalWatch watch;
    armArrival(watch, 0);
    stepArrival(watch, at(1, 0.0f, 50.0f, 0.0f, false));
    stepArrival(watch, at(2, 1000.0f, 202.0f, 1000.0f, false));
    stepArrival(watch, at(3, 1000.0f, 202.0f, 1000.0f, true));
    CHECK(stepArrival(watch, at(4, 1030.0f, 100.0f, 1000.0f, true)) == ArrivalAction::None);
}

void testDeckNeverReady() {
    ArrivalWatch watch;
    armArrival(watch, 0);
    stepArrival(watch, at(1, 0.0f, 50.0f, 0.0f, false));
    stepArrival(watch, at(2, 1000.0f, 202.0f, 1000.0f, false));
    ArrivalAction last = ArrivalAction::None;
    for (int tick = 3; tick < 2000 && watch.phase != ArrivalPhase::Idle; ++tick) {
        last = stepArrival(watch, at(tick, 1000.0f, 20.0f, 1000.0f, false));
    }
    CHECK(last == ArrivalAction::EndedDeckNeverReady);
}

void testNoArrivalTimesOut() {
    ArrivalWatch watch;
    armArrival(watch, 0);
    ArrivalAction last = ArrivalAction::None;
    for (int tick = 1; tick < 5000 && watch.phase != ArrivalPhase::Idle; ++tick) {
        last = stepArrival(watch, at(tick, 0.0f, 50.0f, 0.0f, false));
    }
    CHECK(last == ArrivalAction::EndedNoArrival);
}

// Warping to a camp Link already stands beside: no jump will come.
void testNearbyStartArrivesAtOnce() {
    ArrivalWatch watch;
    armArrival(watch, 0);
    CHECK(stepArrival(watch, at(1, 1010.0f, 200.0f, 1000.0f, true)) == ArrivalAction::Arrived);
}

// Walking into the camp radius without a warp jump is not an arrival.
void testWalkingInIsNotArrival() {
    ArrivalWatch watch;
    armArrival(watch, 0);
    CHECK(stepArrival(watch, at(1, 1040.0f, 200.0f, 1000.0f, false)) == ArrivalAction::None);
    CHECK(stepArrival(watch, at(2, 1035.0f, 200.0f, 1000.0f, false)) == ArrivalAction::None);
    CHECK(stepArrival(watch, at(3, 1029.0f, 200.0f, 1000.0f, false)) == ArrivalAction::None);
}

} // namespace

int main() {
    testLoadingScreenDoesNotEndTheWatch();
    testFallPastDeckIsLifted();
    testLandingOnDeckNeedsNoLift();
    testLiftsAreCapped();
    testFarSidewaysIsNotLifted();
    testDeckNeverReady();
    testNoArrivalTimesOut();
    testNearbyStartArrivesAtOnce();
    testWalkingInIsNotArrival();
    if (gFailures != 0) {
        std::printf("%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("arrival_watch_test OK\n");
    return 0;
}
