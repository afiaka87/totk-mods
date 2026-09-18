// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>

#include "HookshotCues.hpp"

using namespace zonai_hookshot::pure;

TEST_CASE("the three sounded moments are the ones a player notices") {
    CHECK(cueForEvent(Event::TargetingEntered) == kCueAim);
    CHECK(cueForEvent(Event::Committed) == kCueFire);
    CHECK(cueForEvent(Event::ClimbAcquired) == kCueArrive);
}

TEST_CASE("bookkeeping events stay silent") {
    CHECK(cueForEvent(Event::None) == nullptr);
    CHECK(cueForEvent(Event::ConfirmStarted) == nullptr);
    CHECK(cueForEvent(Event::Latched) == nullptr);
    CHECK(cueForEvent(Event::PositionZipStarted) == nullptr);
    CHECK(cueForEvent(Event::PositionZipEnded) == nullptr);
    CHECK(cueForEvent(Event::CaptureStarted) == nullptr);
    CHECK(cueForEvent(Event::HandoffArmed) == nullptr);
    CHECK(cueForEvent(Event::GlideEntered) == nullptr);
    CHECK(cueForEvent(Event::CooldownDone) == nullptr);
}

TEST_CASE("the target answers back only when the answer changes") {
    SUBCASE("finding a good target chimes") {
        CHECK(cueForVerdictChange(Verdict::Miss, Verdict::Valid) == kCueAccept);
    }
    SUBCASE("finding a refused surface buzzes") {
        CHECK(cueForVerdictChange(Verdict::Miss, Verdict::NoClimb) == kCueReject);
        CHECK(cueForVerdictChange(Verdict::Valid, Verdict::TooFar) == kCueReject);
        CHECK(cueForVerdictChange(Verdict::Valid, Verdict::Floor) == kCueReject);
    }
    SUBCASE("holding still is silent") {
        CHECK(cueForVerdictChange(Verdict::Valid, Verdict::Valid) == nullptr);
        CHECK(cueForVerdictChange(Verdict::NoClimb, Verdict::NoClimb) == nullptr);
    }
    SUBCASE("sweeping across open sky is silent") {
        CHECK(cueForVerdictChange(Verdict::Valid, Verdict::Miss) == nullptr);
        CHECK(cueForVerdictChange(Verdict::Valid, Verdict::Pending) == nullptr);
        CHECK(cueForVerdictChange(Verdict::NoClimb, Verdict::InvalidFinite) ==
              nullptr);
    }
}

TEST_CASE("the refusal buzz cannot machine-gun while sweeping") {
    CueRateLimit gate{};
    CHECK(gate.allow(100));
    CHECK_FALSE(gate.allow(101));
    CHECK_FALSE(gate.allow(100 + kCueMinRepeatTicks - 1));
    CHECK(gate.allow(100 + kCueMinRepeatTicks));
    CHECK_FALSE(gate.allow(100 + kCueMinRepeatTicks + 1));
}

TEST_CASE("the first sound of a session is never withheld") {
    CueRateLimit gate{};
    CHECK(gate.allow(0));
}

TEST_CASE("the travel cue is wanted for exactly the carried phases") {
    CHECK(travelCueWanted(Phase::PositionCruise));
    CHECK(travelCueWanted(Phase::Capture));
    CHECK(travelCueWanted(Phase::FallCruise));
    CHECK(travelCueWanted(Phase::GlideHandoff));
    CHECK(travelCueWanted(Phase::GlideTerminal));

    CHECK_FALSE(travelCueWanted(Phase::Idle));
    CHECK_FALSE(travelCueWanted(Phase::Arming));
    CHECK_FALSE(travelCueWanted(Phase::Targeting));
    CHECK_FALSE(travelCueWanted(Phase::Confirming));
    CHECK_FALSE(travelCueWanted(Phase::ChainLaunch));
    CHECK_FALSE(travelCueWanted(Phase::Latched));
    CHECK_FALSE(travelCueWanted(Phase::Cooldown));
}

TEST_CASE("the travel one-shot emits once per trip") {
    TravelCueGate gate{};
    CHECK(gate.shouldEmit(true));
    for (int tick = 0; tick < 600; ++tick)
        CHECK_FALSE(gate.shouldEmit(true));
    CHECK_FALSE(gate.shouldEmit(false));
    CHECK_FALSE(gate.shouldEmit(false));
    CHECK(gate.shouldEmit(true));
}
