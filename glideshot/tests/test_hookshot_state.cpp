// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>

#include "HookshotState.hpp"

using namespace zonai_hookshot::pure;

namespace {

MachineInputs idleInputs() {
    MachineInputs in{};
    in.worldReady = true;
    in.freshInput = true;
    return in;
}

MachineInputs chordInputs() {
    auto in = idleInputs();
    in.chordHeld = true;
    in.stickClickHeld = true;
    return in;
}

Machine targetingMachine(const MachineConfig& c = {}) {
    Machine m{};
    for (int i = 0; i <= c.armHoldTicks; ++i) step(m, chordInputs(), c);
    REQUIRE(m.phase == Phase::Targeting);
    auto in = idleInputs();  // chord + stick click released
    step(m, in, c);
    return m;
}

}  // namespace

TEST_CASE("arming requires the deliberate hold and enters targeting") {
    Machine m{};
    MachineConfig c{};
    auto out = step(m, chordInputs(), c);
    CHECK(m.phase == Phase::Arming);
    CHECK(out.event == Event::None);
    for (int i = 0; i < c.armHoldTicks; ++i) {
        out = step(m, chordInputs(), c);
    }
    CHECK(out.event == Event::TargetingEntered);
    CHECK(m.phase == Phase::Targeting);
}

TEST_CASE("the final arming tick still consumes the right-stick click") {
    Machine m{};
    MachineConfig c{};
    StepOutput out{};
    for (int i = 0; i <= c.armHoldTicks; ++i) out = step(m, chordInputs(), c);
    CHECK(m.phase == Phase::Targeting);
    CHECK(out.event == Event::TargetingEntered);
    CHECK(out.consumed.stickClick);
}

TEST_CASE("right-stick click stays masked until physical release") {
    Machine m{};
    MachineConfig c{};
    for (int i = 0; i <= c.armHoldTicks; ++i) step(m, chordInputs(), c);
    CHECK(m.phase == Phase::Targeting);

    auto in = idleInputs();
    in.stickClickHeld = true;
    auto out = step(m, in, c);
    CHECK(out.consumed.stickClick);

    out = step(m, idleInputs(), c);
    CHECK_FALSE(out.consumed.stickClick);

    in = idleInputs();
    in.stickClickHeld = true;
    out = step(m, in, c);
    CHECK_FALSE(out.consumed.stickClick);
}

TEST_CASE("arming abandoned keeps masking the held stick click") {
    Machine m{};
    MachineConfig c{};
    step(m, chordInputs(), c);
    CHECK(m.phase == Phase::Arming);

    auto in = idleInputs();
    in.stickClickHeld = true;
    auto out = step(m, in, c);
    CHECK(out.event == Event::ArmingAbandoned);
    CHECK(m.phase == Phase::Idle);
    CHECK(out.consumed.stickClick);

    out = step(m, idleInputs(), c);
    CHECK_FALSE(out.consumed.stickClick);
}

TEST_CASE("stale controller frames advance nothing") {
    Machine m{};
    MachineConfig c{};
    step(m, chordInputs(), c);
    CHECK(m.phase == Phase::Arming);
    const int held = m.armTicks;

    MachineInputs stale{};
    stale.worldReady = true;
    stale.freshInput = false;
    stale.chordHeld = true;       // last known state
    stale.stickClickHeld = true;
    for (int i = 0; i < 50; ++i) step(m, stale, c);
    CHECK(m.phase == Phase::Arming);
    CHECK(m.armTicks == held);
}

TEST_CASE("targeting A press starts confirmation and consumes A") {
    MachineConfig c{};
    auto m = targetingMachine(c);
    auto in = idleInputs();
    in.aEdge = true;
    auto out = step(m, in, c);
    CHECK(out.event == Event::ConfirmStarted);
    CHECK(m.phase == Phase::Confirming);
    CHECK(out.consumed.a);
}

TEST_CASE("confirmation commits on a fresh post-press sample") {
    MachineConfig c{};
    auto m = targetingMachine(c);
    auto in = idleInputs();
    in.aEdge = true;
    step(m, in, c);

    in = idleInputs();
    in.confirm = ConfirmDecision::Commit;
    auto out = step(m, in, c);
    CHECK(out.event == Event::Committed);
    CHECK(m.phase == Phase::ChainLaunch);
}

TEST_CASE("confirmation refusal returns to targeting") {
    MachineConfig c{};
    auto m = targetingMachine(c);
    auto in = idleInputs();
    in.aEdge = true;
    step(m, in, c);

    in = idleInputs();
    in.confirm = ConfirmDecision::Refuse;
    auto out = step(m, in, c);
    CHECK(out.event == Event::Refused);
    CHECK(m.phase == Phase::Targeting);
}

TEST_CASE("confirmation times out bounded") {
    MachineConfig c{};
    auto m = targetingMachine(c);
    auto in = idleInputs();
    in.aEdge = true;
    step(m, in, c);

    StepOutput out{};
    for (int i = 0; i < c.confirmTimeoutTicks; ++i) {
        CHECK(m.phase == Phase::Confirming);
        out = step(m, idleInputs(), c);
    }
    CHECK(out.event == Event::Refused);
    CHECK(m.phase == Phase::Targeting);
}

TEST_CASE("B cancels targeting and the cancel press is consumed") {
    MachineConfig c{};
    auto m = targetingMachine(c);
    auto in = idleInputs();
    in.bEdge = true;
    auto out = step(m, in, c);
    CHECK(out.event == Event::Cancelled);
    CHECK(m.phase == Phase::Cooldown);
    CHECK(out.consumed.b);
}

TEST_CASE("B cancels confirmation") {
    MachineConfig c{};
    auto m = targetingMachine(c);
    auto in = idleInputs();
    in.aEdge = true;
    step(m, in, c);
    in = idleInputs();
    in.bEdge = true;
    auto out = step(m, in, c);
    CHECK(out.event == Event::Cancelled);
    CHECK(m.phase == Phase::Cooldown);
    CHECK(out.consumed.b);
}

TEST_CASE("launch publishes one latch beat then auto-zips without another A") {
    MachineConfig c{};
    auto m = targetingMachine(c);
    auto in = idleInputs();
    in.aEdge = true;
    step(m, in, c);
    in = idleInputs();
    in.confirm = ConfirmDecision::Commit;
    step(m, in, c);
    CHECK(m.phase == Phase::ChainLaunch);

    in = idleInputs();
    in.launchComplete = true;
    auto out = step(m, in, c);
    CHECK(out.event == Event::Latched);
    CHECK(m.phase == Phase::Latched);

    in = idleInputs();
    out = step(m, in, c);
    CHECK(out.event == Event::PositionZipStarted);
    CHECK(m.phase == Phase::PositionCruise);
    CHECK(out.consumed.a);
    CHECK(out.consumed.b);
}

TEST_CASE("B can cancel the one-tick automatic latch beat") {
    MachineConfig c{};
    auto m = targetingMachine(c);
    auto in = idleInputs();
    in.aEdge = true;
    step(m, in, c);
    in = idleInputs();
    in.confirm = ConfirmDecision::Commit;
    step(m, in, c);
    in = idleInputs();
    in.launchComplete = true;
    step(m, in, c);
    REQUIRE(m.phase == Phase::Latched);

    in = idleInputs();
    in.bEdge = true;
    const auto out = step(m, in, c);
    CHECK(out.event == Event::Cleared);
    CHECK(m.phase == Phase::Cooldown);
    CHECK(out.consumed.b);
}

TEST_CASE("cooldown counts down and returns to idle") {
    MachineConfig c{};
    auto m = targetingMachine(c);
    auto in = idleInputs();
    in.bEdge = true;
    step(m, in, c);
    CHECK(m.phase == Phase::Cooldown);

    StepOutput out{};
    for (int i = 0; i < c.cooldownTicks; ++i) {
        CHECK(m.phase == Phase::Cooldown);
        out = step(m, idleInputs(), c);
    }
    CHECK(out.event == Event::CooldownDone);
    CHECK(m.phase == Phase::Idle);
}

TEST_CASE("world loss resets fail-closed from every active phase") {
    MachineConfig c{};
    MachineInputs lost{};
    lost.worldReady = false;
    lost.freshInput = true;

    SUBCASE("from arming - latch survives while the button stays held") {
        Machine m{};
        step(m, chordInputs(), c);
        CHECK(m.stickClickLatched);
        auto lostHeld = lost;
        lostHeld.stickClickHeld = true;
        auto out = step(m, lostHeld, c);
        CHECK(out.event == Event::Reset);
        CHECK(m.phase == Phase::Idle);
        CHECK(m.stickClickLatched);
        CHECK(out.consumed.stickClick);
        out = step(m, lostHeld, c);
        CHECK(out.consumed.stickClick);
        out = step(m, lost, c);
        CHECK_FALSE(m.stickClickLatched);
        CHECK_FALSE(out.consumed.stickClick);
    }
    SUBCASE("from arming - stale samples during world loss keep the latch") {
        Machine m{};
        step(m, chordInputs(), c);
        auto lostStale = lost;
        lostStale.freshInput = false;
        lostStale.stickClickHeld = true;  // last known state
        step(m, lostStale, c);
        auto out = step(m, lostStale, c);
        CHECK(m.stickClickLatched);
        CHECK(out.consumed.stickClick);
    }
    SUBCASE("from targeting") {
        auto m = targetingMachine(c);
        auto out = step(m, lost, c);
        CHECK(out.event == Event::Reset);
        CHECK(m.phase == Phase::Idle);
    }
    SUBCASE("from confirming") {
        auto m = targetingMachine(c);
        auto in = idleInputs();
        in.aEdge = true;
        step(m, in, c);
        auto out = step(m, lost, c);
        CHECK(out.event == Event::Reset);
        CHECK(m.phase == Phase::Idle);
    }
    SUBCASE("from latched") {
        auto m = targetingMachine(c);
        auto in = idleInputs();
        in.aEdge = true;
        step(m, in, c);
        in = idleInputs();
        in.confirm = ConfirmDecision::Commit;
        step(m, in, c);
        in = idleInputs();
        in.launchComplete = true;
        step(m, in, c);
        CHECK(m.phase == Phase::Latched);
        auto out = step(m, lost, c);
        CHECK(out.event == Event::Reset);
        CHECK(m.phase == Phase::Idle);
    }
    SUBCASE("idle world loss is quiet") {
        Machine m{};
        auto out = step(m, lost, c);
        CHECK(out.event == Event::None);
        CHECK(m.phase == Phase::Idle);
    }
}

TEST_CASE("no buttons are owned outside active states") {
    CHECK_FALSE(ownedButtons(Phase::Idle).stickClick);
    CHECK_FALSE(ownedButtons(Phase::Idle).a);
    CHECK_FALSE(ownedButtons(Phase::Idle).b);
    CHECK_FALSE(ownedButtons(Phase::Cooldown).stickClick);
    CHECK_FALSE(ownedButtons(Phase::Cooldown).a);
    CHECK_FALSE(ownedButtons(Phase::Cooldown).b);

    Machine m{};
    auto out = step(m, idleInputs());
    CHECK_FALSE(out.consumed.stickClick);
    CHECK_FALSE(out.consumed.a);
    CHECK_FALSE(out.consumed.b);
}

TEST_CASE("targeting owns A and B every tick") {
    MachineConfig c{};
    auto m = targetingMachine(c);
    auto out = step(m, idleInputs(), c);
    CHECK(out.consumed.a);
    CHECK(out.consumed.b);
    CHECK_FALSE(out.consumed.stickClick);
}

namespace {

Machine latchedMachine(const MachineConfig& c = {}) {
    auto m = targetingMachine(c);
    auto in = idleInputs();
    in.aEdge = true;
    step(m, in, c);
    in = idleInputs();
    in.confirm = ConfirmDecision::Commit;
    step(m, in, c);
    in = idleInputs();
    in.launchComplete = true;
    step(m, in, c);
    REQUIRE(m.phase == Phase::Latched);
    return m;
}

}  // namespace

namespace {

Machine fallCruiseMachine(const MachineConfig& c = {}) {
    MachineConfig legacy = c;
    legacy.positionDriveEnabled = false;
    auto m = latchedMachine(legacy);
    auto in = idleInputs();
    in.aEdge = true;
    step(m, in, legacy);
    in = idleInputs();
    in.fallEntered = true;
    step(m, in, legacy);
    REQUIRE(m.phase == Phase::FallCruise);
    return m;
}

}  // namespace

TEST_CASE("A while latched starts the exact position zip and is consumed") {
    MachineConfig c{};
    auto m = latchedMachine(c);
    auto in = idleInputs();
    in.aEdge = true;
    auto out = step(m, in, c);
    CHECK(out.event == Event::PositionZipStarted);
    CHECK(m.phase == Phase::PositionCruise);
    CHECK(out.consumed.a);
    CHECK(out.consumed.b);
}

TEST_CASE("position zip waits for Parasail then enters terminal capture") {
    MachineConfig c{};
    SUBCASE("endpoint alone holds position cruise") {
        auto m = latchedMachine(c);
        auto in = idleInputs();
        in.aEdge = true;
        step(m, in, c);
        in = idleInputs();
        in.positionZipDone = true;
        const auto out = step(m, in, c);
        CHECK(out.event == Event::None);
        CHECK(m.phase == Phase::PositionCruise);
    }
    SUBCASE("endpoint plus native Parasail starts capture") {
        auto m = latchedMachine(c);
        auto in = idleInputs();
        in.aEdge = true;
        step(m, in, c);
        in = idleInputs();
        in.positionZipDone = true;
        in.glideEntered = true;
        const auto out = step(m, in, c);
        CHECK(out.event == Event::CaptureStarted);
        CHECK(m.phase == Phase::Capture);
    }
    SUBCASE("invalid or timeout failed") {
        auto m = latchedMachine(c);
        auto in = idleInputs();
        in.aEdge = true;
        step(m, in, c);
        in = idleInputs();
        in.positionZipFailed = true;
        const auto out = step(m, in, c);
        CHECK(out.event == Event::PositionZipFailed);
        CHECK(m.phase == Phase::Cooldown);
    }
}

TEST_CASE("capture releases immediately on native Climb and fails bounded") {
    MachineConfig c{};
    auto captureMachine = [&] {
        auto m = latchedMachine(c);
        auto in = idleInputs();
        in.aEdge = true;
        step(m, in, c);
        in = idleInputs();
        in.positionZipDone = true;
        in.glideEntered = true;
        step(m, in, c);
        REQUIRE(m.phase == Phase::Capture);
        return m;
    };
    SUBCASE("native Climb wins") {
        auto m = captureMachine();
        auto in = idleInputs();
        in.climbEntered = true;
        const auto out = step(m, in, c);
        CHECK(out.event == Event::ClimbAcquired);
        CHECK(m.phase == Phase::Cooldown);
        CHECK(out.consumed.a);
        CHECK(out.consumed.b);
    }
    SUBCASE("bounded capture failure") {
        auto m = captureMachine();
        auto in = idleInputs();
        in.captureFailed = true;
        const auto out = step(m, in, c);
        CHECK(out.event == Event::CaptureFailed);
        CHECK(m.phase == Phase::Cooldown);
    }
    SUBCASE("B cancels") {
        auto m = captureMachine();
        auto in = idleInputs();
        in.bEdge = true;
        const auto out = step(m, in, c);
        CHECK(out.event == Event::Cleared);
        CHECK(m.phase == Phase::Cooldown);
    }
}

TEST_CASE("B cancels the position zip and world loss resets it") {
    MachineConfig c{};
    auto m = latchedMachine(c);
    auto in = idleInputs();
    in.aEdge = true;
    step(m, in, c);
    REQUIRE(m.phase == Phase::PositionCruise);
    in = idleInputs();
    in.bEdge = true;
    auto out = step(m, in, c);
    CHECK(out.event == Event::Cleared);
    CHECK(m.phase == Phase::Cooldown);
    CHECK(out.consumed.b);

    m = latchedMachine(c);
    in = idleInputs();
    in.aEdge = true;
    step(m, in, c);
    MachineInputs lost{};
    lost.worldReady = false;
    lost.freshInput = true;
    out = step(m, lost, c);
    CHECK(out.event == Event::Reset);
    CHECK(m.phase == Phase::Idle);
}
