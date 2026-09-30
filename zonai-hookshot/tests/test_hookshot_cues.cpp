// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <array>
#include <cstring>
#include "../../arrowbound/src/engine/SoundHandle.hpp"

#include "HookshotCues.hpp"

using namespace zonai_hookshot::pure;

TEST_CASE("an empty allocated sound event cannot suppress the fallback") {
    AbilityCueGate gate{};
    for (int i = 0; i < 3; ++i) CHECK_FALSE(gate.needsFallback(true, 0));
    CHECK(gate.needsFallback(true, 0));
    CHECK_FALSE(gate.needsFallback(true, 0));
    CHECK_FALSE(gate.needsFallback(false, 0));
    gate = {};
    CHECK(gate.needsFallback(false, 0));
    CHECK_FALSE(gate.needsFallback(false, 0));
}

TEST_CASE("a started ability sound plays once without a late fallback") {
    AbilityCueGate gate{};
    CHECK_FALSE(gate.needsFallback(true, 0));
    CHECK_FALSE(gate.needsFallback(true, 1));
    CHECK(gate.resolved);
    CHECK_FALSE(gate.needsFallback(false, 0)); // Normal completion is not a failure.
    gate = {};
    CHECK_FALSE(gate.needsFallback(true, 2));
    CHECK(gate.resolved);
}

TEST_CASE("sound handles use native system index and create-id fields") {
    using namespace arrowbound::engine;
    SoundHandle handle{};
    CHECK(handle.index == -1);
    CHECK(handle.createId == 0);
    const std::array<unsigned char, 8> native{1, 0, 0x23, 0x01, 0x78, 0x56, 0x34, 0x12};
    std::memcpy(&handle, native.data(), native.size());
    CHECK(handle.system == 1);
    CHECK(handle.index == 0x123);
    CHECK(handle.createId == 0x12345678);
    SoundHandle retained = handle;
    CHECK(retained.createId == handle.createId);
}

TEST_CASE("only manual targeting emits reticle feedback") {
    AimFeedbackState manual;
    const Verdict sequence[]{Verdict::Pending, Verdict::Miss, Verdict::Valid, Verdict::Valid,
        Verdict::NoClimb, Verdict::Valid, Verdict::Floor, Verdict::Miss, Verdict::Floor};
    std::uint32_t tick = 0;
    for (auto verdict : sequence) {
        const auto a = manual.update(aimFeedbackInput(Phase::Targeting, verdict), tick);
        if (tick == 0) CHECK(a.entry == kCueAim);
        if (tick == 2) CHECK(a.verdict == kCueAccept);
        if (tick == 4) CHECK(a.verdict == kCueReject);
        if (tick == 6 || tick == 8) CHECK(a.verdict == nullptr);
        ++tick;
    }
    const auto traveling = aimFeedbackInput(Phase::ChainLaunch, Verdict::Valid);
    CHECK_FALSE(traveling.active);
    CHECK_FALSE(aimFeedbackInput(Phase::Idle, Verdict::Valid).active);
    CHECK(aimFeedbackInput(Phase::Confirming, Verdict::Floor).verdict == Verdict::Floor);
}

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
