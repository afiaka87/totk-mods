#include <doctest.h>

#include "RecallLogic.hpp"

using namespace self_recall::pure;

namespace {

struct SimRecorder {
    IdleCropState idle{};
    std::uint16_t head = 0;
    std::uint16_t count = 0;
    std::uint64_t now = 0;
    std::uint64_t lastStoredTick = 0;
    bool haveStored = false;
    std::uint16_t lastWrittenIndex = 0xffff;
    std::uint16_t lastWrittenDelta = 0;

    RecordDecision update(bool moved) {
        ++now;
        const RecordDecision decision = idleCropStep(idle, moved);
        if (decision == RecordDecision::SkipIdle) return decision;
        std::uint16_t delta = 1;
        if (haveStored) {
            delta = decision == RecordDecision::ResumeAfterCrop
                        ? std::uint16_t(1)
                        : clampTickDelta(now - lastStoredTick);
        }
        lastWrittenIndex = head;
        lastWrittenDelta = delta;
        head = static_cast<std::uint16_t>((head + 1) % kHistoryCapacity);
        if (count < kHistoryCapacity) ++count;
        lastStoredTick = now;
        haveStored = true;
        return decision;
    }
};

}  // namespace

TEST_CASE("the configured ring reverses cleanly across wrap") {
    std::uint16_t index = 0;
    index = previous(index);
    CHECK(index == kHistoryCapacity - 1);
    for (int i = 0; i < kHistoryCapacity - 1; ++i) index = previous(index);
    CHECK(index == 0);
}

TEST_CASE("recorded timing preserves slow and fast route speeds") {
    CHECK(recordedPathSpeed(0.1f, 1) ==
          doctest::Approx(6.0f));
    CHECK(recordedPathSpeed(0.2f, 1) ==
          doctest::Approx(12.0f));
    CHECK(recordedPathSpeed(0.2f, 2) ==
          doctest::Approx(6.0f));
    CHECK(replayDelayAfterApply(1) == 0);
    CHECK(replayDelayAfterApply(3) == 2);
}

TEST_CASE("tick deltas are never zero and saturate safely") {
    CHECK(clampTickDelta(0) == 1);
    CHECK(clampTickDelta(1) == 1);
    CHECK(clampTickDelta(65535) == 65535);
    CHECK(clampTickDelta(65536) == 65535);
}

TEST_CASE("only impossible gameplay jumps invalidate the route") {
    CHECK_FALSE(isTeleportDiscontinuity(29.9f));
    CHECK_FALSE(isTeleportDiscontinuity(30.0f));
    CHECK(isTeleportDiscontinuity(30.1f));
}

TEST_CASE("engine-step allowance forgives fast falls and not slow walks") {
    const float diveAllowance =
        correctionAllowanceMeters(0.0f, 53.9f, 107.4f);
    CHECK(diveAllowance == doctest::Approx(0.75f + 107.4f / 30.0f));
    CHECK(1.79f <= diveAllowance);

    CHECK(correctionAllowanceMeters(107.4f, 0.0f, 0.0f) ==
          doctest::Approx(diveAllowance));
    CHECK(correctionAllowanceMeters(0.0f, 107.4f, 0.0f) ==
          doctest::Approx(diveAllowance));

    const float walkAllowance =
        correctionAllowanceMeters(1.5f, 1.0f, 1.5f);
    CHECK(walkAllowance == doctest::Approx(0.80f));
    CHECK(0.89f > walkAllowance);

    CHECK(9.0f > diveAllowance);

    CHECK(correctionAllowanceMeters(0.0f, 0.0f, 0.0f) ==
          doctest::Approx(kCorrectionBaseMeters));
    CHECK(correctionAllowanceMeters(-1.0f, -2.0f, -3.0f) ==
          doctest::Approx(kCorrectionBaseMeters));

    CHECK(kBlockPersistTicks > 1);
}

TEST_CASE("windowed path speed survives the half-rate world step") {
    CHECK(recordedPathSpeed(0.1f, 2) == doctest::Approx(3.0f));
    CHECK(carriedPathSpeed(3.0f, 1) == doctest::Approx(3.0f));
    CHECK(carriedPathSpeed(3.0f, 2) == doctest::Approx(3.0f));
    CHECK(carriedPathSpeed(3.0f, 3) == 0.0f);
    CHECK(carriedPathSpeed(3.0f, 400) == 0.0f);
}

TEST_CASE("idle cropping keeps exactly three seconds of stillness") {
    IdleCropState idle{};
    for (int i = 0; i < 180; ++i) {
        CAPTURE(i);
        CHECK(idleCropStep(idle, false) == RecordDecision::Record);
    }
    CHECK(idleCropStep(idle, false) == RecordDecision::SkipIdle);
    for (int i = 0; i < 10000; ++i) {
        if (idleCropStep(idle, false) != RecordDecision::SkipIdle) {
            FAIL("stationary update consumed a slot while cropped");
        }
    }
    CHECK(idleCropStep(idle, true) == RecordDecision::ResumeAfterCrop);
    CHECK(idleCropStep(idle, true) == RecordDecision::Record);
    CHECK(idleCropStep(idle, false) == RecordDecision::Record);
}

TEST_CASE("a cropped ring freezes and resumes on the expected slot") {
    SimRecorder sim;
    for (int i = 0; i < 100; ++i) sim.update(true);
    CHECK(sim.count == 100);
    CHECK(sim.head == 100);

    for (int i = 0; i < 180; ++i) sim.update(false);
    CHECK(sim.count == 280);
    CHECK(sim.head == 280);

    for (int i = 0; i < 5000; ++i) sim.update(false);
    CHECK(sim.count == 280);
    CHECK(sim.head == 280);

    CHECK(sim.update(true) == RecordDecision::ResumeAfterCrop);
    CHECK(sim.lastWrittenIndex == 280);
    CHECK(sim.lastWrittenDelta == 1);
    CHECK(sim.count == 281);
    CHECK(sim.head == 281);
}

TEST_CASE("a full ring stays frozen while idle-cropped") {
    SimRecorder sim;
    for (std::uint32_t i = 0; i < kHistoryCapacity + 25u; ++i)
        sim.update(true);
    CHECK(sim.count == kHistoryCapacity);
    const std::uint16_t frozenHead = sim.head;
    CHECK(frozenHead == 25);

    for (int i = 0; i < 180; ++i) sim.update(false);
    for (int i = 0; i < 3000; ++i) sim.update(false);
    CHECK(sim.count == kHistoryCapacity);
    CHECK(sim.head ==
          static_cast<std::uint16_t>((frozenHead + 180) % kHistoryCapacity));

    const std::uint16_t resumeSlot = sim.head;
    CHECK(sim.update(true) == RecordDecision::ResumeAfterCrop);
    CHECK(sim.lastWrittenIndex == resumeSlot);
    CHECK(sim.lastWrittenDelta == 1);
}

TEST_CASE("rotation and state changes count as motion for idle cropping") {
    CHECK_FALSE(idleMotionExceeded(0.0f, 0, false));
    CHECK_FALSE(idleMotionExceeded(0.009f, 250, false));

    CHECK(idleMotionExceeded(0.01f, 0, false));

    CHECK(idleMotionExceeded(0.0f, 500, false));
    CHECK(idleMotionExceeded(0.0f, -500, false));

    CHECK(idleMotionExceeded(0.0f, 0, true));
}
