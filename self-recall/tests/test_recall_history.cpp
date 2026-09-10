
#include <cstddef>
#include <limits>
#include <memory>

#include "RecallAnimationPolicy.hpp"
#include "RecallHistory.hpp"
#include "doctest.h"

using namespace self_recall::pure;

namespace {

RecordInput at(float x, float y, float z, std::uint64_t tick) {
    RecordInput input{};
    input.pose.rotation.values[0] = 1.0f;
    input.pose.rotation.values[4] = 1.0f;
    input.pose.rotation.values[8] = 1.0f;
    input.pose.position = {x, y, z};
    input.tick = tick;
    input.admissible = true;
    return input;
}

struct Fixture {
    RouteHistory history{};
    RecorderState recorder{};
};

}  // namespace

TEST_CASE("recorded sample format is frozen") {
    CHECK(sizeof(Pose) == 48);
    CHECK(sizeof(HistorySample) == 104);
    CHECK(kHistoryCapacity == 3840);
    CHECK(kMinHistory == 90);

    CHECK(offsetof(HistorySample, pose) == 0);
    CHECK(offsetof(HistorySample, engineVelocity) == 48);
    CHECK(offsetof(HistorySample, pathSpeed) == 60);
    CHECK(offsetof(HistorySample, recordedTick) == 64);
    CHECK(offsetof(HistorySample, serial) == 72);
    CHECK(offsetof(HistorySample, tickDelta) == 76);
    CHECK(offsetof(HistorySample, flags) == 78);
    CHECK(offsetof(HistorySample, animKind) == 79);
    CHECK(offsetof(HistorySample, animSlot) == 80);
    CHECK(offsetof(HistorySample, animFrame) == 84);
    CHECK(offsetof(HistorySample, animRate) == 88);
    CHECK(offsetof(HistorySample, stickX) == 92);
    CHECK(offsetof(HistorySample, stickY) == 96);

    CHECK(offsetof(Pose, rotation) == 0);
    CHECK(offsetof(Pose, position) == 36);

    CHECK(SampleAdmissible == 1u);
    CHECK(SampleClimb == 2u);
}

TEST_CASE("a recorded sample carries the update verbatim") {
    auto fixture = std::make_unique<Fixture>();
    RecordInput input = at(1.0f, 2.0f, 3.0f, 100);
    input.engineVelocity[0] = 4.0f;
    input.engineVelocity[1] = -5.0f;
    input.engineVelocity[2] = 6.0f;
    input.animKind = kKindMove;
    input.animSlot = 2;
    input.animFrame = 12.5f;
    input.animRate = 1.25f;
    input.stickX = -20000;
    input.stickY = 30000;
    input.climbing = true;

    const RecordReport report =
        recordSample(fixture->recorder, fixture->history, input);
    REQUIRE(report.result == RecordResult::Recorded);
    REQUIRE(fixture->history.count == 1);

    const HistorySample& stored = fixture->history.samples[0];
    CHECK(stored.pose.position.x == doctest::Approx(1.0f));
    CHECK(stored.pose.position.y == doctest::Approx(2.0f));
    CHECK(stored.pose.position.z == doctest::Approx(3.0f));
    CHECK(stored.engineVelocity[0] == doctest::Approx(4.0f));
    CHECK(stored.engineVelocity[1] == doctest::Approx(-5.0f));
    CHECK(stored.engineVelocity[2] == doctest::Approx(6.0f));
    CHECK(stored.recordedTick == 100);
    CHECK(stored.serial == 1);
    CHECK(stored.tickDelta == 1);
    CHECK(stored.animKind == kKindMove);
    CHECK(stored.animSlot == 2);
    CHECK(stored.animFrame == doctest::Approx(12.5f));
    CHECK(stored.animRate == doctest::Approx(1.25f));
    CHECK(stored.stickX == -20000);
    CHECK(stored.stickY == 30000);
    CHECK((stored.flags & SampleAdmissible) != 0);
    CHECK((stored.flags & SampleClimb) != 0);
}

TEST_CASE("an inadmissible update is stored but not reachable") {
    auto fixture = std::make_unique<Fixture>();
    RecordInput input = at(0.0f, 0.0f, 0.0f, 1);
    input.admissible = false;
    recordSample(fixture->recorder, fixture->history, input);
    CHECK((fixture->history.samples[0].flags & SampleAdmissible) == 0);
    CHECK(reachableAdmissiblePrefix(fixture->history) == 0);

    RecordInput next = at(0.0f, 0.0f, 1.0f, 2);
    recordSample(fixture->recorder, fixture->history, next);
    CHECK(reachableAdmissiblePrefix(fixture->history) == 1);
}

TEST_CASE("non-finite engine velocity is filtered to zero, not stored") {
    auto fixture = std::make_unique<Fixture>();
    RecordInput input = at(0.0f, 0.0f, 0.0f, 1);
    input.engineVelocity[1] = std::numeric_limits<float>::infinity();
    recordSample(fixture->recorder, fixture->history, input);
    CHECK(fixture->history.samples[0].engineVelocity[1] == doctest::Approx(0.0f));
}

TEST_CASE("logical tick spacing follows the recorder clock") {
    auto fixture = std::make_unique<Fixture>();
    recordSample(fixture->recorder, fixture->history, at(0.0f, 0.0f, 0.0f, 10));
    recordSample(fixture->recorder, fixture->history, at(1.0f, 0.0f, 0.0f, 11));
    recordSample(fixture->recorder, fixture->history, at(2.0f, 0.0f, 0.0f, 20));

    CHECK(fixture->history.samples[0].tickDelta == 1);  // first sample
    CHECK(fixture->history.samples[1].tickDelta == 1);
    CHECK(fixture->history.samples[2].tickDelta == 9);
}

TEST_CASE("path speed windows over DISTINCT positions") {
    auto fixture = std::make_unique<Fixture>();
    recordSample(fixture->recorder, fixture->history, at(0.0f, 0.0f, 0.0f, 1));
    recordSample(fixture->recorder, fixture->history, at(0.1f, 0.0f, 0.0f, 3));
    CHECK(fixture->history.samples[1].pathSpeed == doctest::Approx(3.0f));

    recordSample(fixture->recorder, fixture->history, at(0.1f, 0.0f, 0.0f, 4));
    CHECK(fixture->history.samples[2].pathSpeed == doctest::Approx(3.0f));

    recordSample(fixture->recorder, fixture->history, at(0.1f, 0.0f, 0.0f, 6));
    CHECK(fixture->history.samples[3].pathSpeed == doctest::Approx(0.0f));
}

TEST_CASE("idle cropping stops consuming capacity after three seconds") {
    auto fixture = std::make_unique<Fixture>();
    std::uint64_t tick = 1;

    for (int i = 0; i < 181; ++i) {
        const RecordReport report = recordSample(fixture->recorder,
                                                 fixture->history,
                                                 at(0.0f, 0.0f, 0.0f, tick++));
        REQUIRE(report.result == RecordResult::Recorded);
    }
    CHECK(fixture->history.count == 181);
    CHECK_FALSE(fixture->recorder.idleCrop.skipping);

    const RecordReport first = recordSample(fixture->recorder, fixture->history,
                                            at(0.0f, 0.0f, 0.0f, tick));
    CHECK(first.result == RecordResult::StartedCropping);
    const std::uint64_t cropStart = tick;
    ++tick;

    for (int i = 0; i < 20; ++i) {
        const RecordReport report = recordSample(fixture->recorder,
                                                 fixture->history,
                                                 at(0.0f, 0.0f, 0.0f, tick++));
        CHECK(report.result == RecordResult::SkippedIdle);
    }
    CHECK(fixture->history.count == 181);
    CHECK(fixture->recorder.latestRecordedSpeed == doctest::Approx(0.0f));

    const RecordReport resumed = recordSample(
        fixture->recorder, fixture->history, at(1.0f, 0.0f, 0.0f, tick));
    CHECK(resumed.result == RecordResult::ResumedAfterCrop);
    CHECK(resumed.skippedTicks == tick - cropStart);
    CHECK(fixture->history.count == 182);
    CHECK(fixture->history.samples[181].tickDelta == 1);
    CHECK_FALSE(fixture->recorder.idleCrop.skipping);
}

TEST_CASE("stillness is judged against a FIXED anchor, so slow drift counts") {
    auto fixture = std::make_unique<Fixture>();
    std::uint64_t tick = 1;
    recordSample(fixture->recorder, fixture->history, at(0.0f, 0.0f, 0.0f, tick++));

    const RecordReport small = recordSample(
        fixture->recorder, fixture->history, at(0.005f, 0.0f, 0.0f, tick++));
    CHECK(small.result == RecordResult::Recorded);
    CHECK(fixture->recorder.idleCrop.stationaryUpdates == 1);

    const RecordReport drifted = recordSample(
        fixture->recorder, fixture->history, at(0.010f, 0.0f, 0.0f, tick++));
    CHECK(drifted.result == RecordResult::Recorded);
    CHECK(fixture->recorder.idleCrop.stationaryUpdates == 0);
}

TEST_CASE("rotation in place and a state-class change both count as action") {
    SUBCASE("yaw") {
        auto fixture = std::make_unique<Fixture>();
        recordSample(fixture->recorder, fixture->history, at(0.0f, 0.0f, 0.0f, 1));
        recordSample(fixture->recorder, fixture->history, at(0.0f, 0.0f, 0.0f, 2));
        CHECK(fixture->recorder.idleCrop.stationaryUpdates == 1);

        RecordInput turned = at(0.0f, 0.0f, 0.0f, 3);
        turned.pose.rotation.values[2] = 0.1f;
        turned.pose.rotation.values[8] = -1.0f;
        recordSample(fixture->recorder, fixture->history, turned);
        CHECK(fixture->recorder.idleCrop.stationaryUpdates == 0);
    }
    SUBCASE("state class") {
        auto fixture = std::make_unique<Fixture>();
        recordSample(fixture->recorder, fixture->history, at(0.0f, 0.0f, 0.0f, 1));
        recordSample(fixture->recorder, fixture->history, at(0.0f, 0.0f, 0.0f, 2));
        CHECK(fixture->recorder.idleCrop.stationaryUpdates == 1);

        RecordInput classChanged = at(0.0f, 0.0f, 0.0f, 3);
        classChanged.stateClass = 3;
        recordSample(fixture->recorder, fixture->history, classChanged);
        CHECK(fixture->recorder.idleCrop.stationaryUpdates == 0);
    }
}

TEST_CASE("the ring wraps at capacity and keeps the newest route") {
    auto fixture = std::make_unique<Fixture>();
    const int total = kHistoryCapacity + 5;
    for (int i = 0; i < total; ++i) {
        recordSample(fixture->recorder, fixture->history,
                     at(static_cast<float>(i), 0.0f, 0.0f,
                        static_cast<std::uint64_t>(i + 1)));
    }
    CHECK(fixture->history.count == kHistoryCapacity);
    CHECK(fixture->history.head == 5);
    CHECK(fixture->history.nextSerial == static_cast<std::uint32_t>(total));

    const HistorySample& newest =
        fixture->history.samples[previous(fixture->history.head)];
    CHECK(newest.pose.position.x == doctest::Approx(static_cast<float>(total - 1)));
    CHECK(newest.serial == static_cast<std::uint32_t>(total));
    CHECK(reachableAdmissiblePrefix(fixture->history) == kHistoryCapacity);
}

TEST_CASE("route statistics and the animation histogram read the whole ring") {
    auto fixture = std::make_unique<Fixture>();
    for (int i = 0; i < 61; ++i) {
        RecordInput input = at(static_cast<float>(i) * 0.5f, 0.0f, 0.0f,
                               static_cast<std::uint64_t>(i + 1));
        input.animKind = (i % 2) == 0 ? kKindMove : kKindFall;
        recordSample(fixture->recorder, fixture->history, input);
    }

    const RouteStats stats = routeStats(fixture->history);
    CHECK(stats.seconds == doctest::Approx(61.0f / 60.0f));
    CHECK(stats.peakSpeed == doctest::Approx(30.0f));
    CHECK(stats.meters == doctest::Approx(30.0f));

    const AnimationHistogram histogram = animationHistogram(fixture->history);
    CHECK(histogram.counts[kKindMove] == 31);
    CHECK(histogram.counts[kKindFall] == 30);
    CHECK(histogram.counts[kKindNone] == 0);
}

TEST_CASE("a generation boundary reseeds the speed window; a route clear does not") {
    const auto seed = [](Fixture& fixture) {
        recordSample(fixture.recorder, fixture.history, at(0.0f, 0.0f, 0.0f, 1));
        recordSample(fixture.recorder, fixture.history, at(0.1f, 0.0f, 0.0f, 3));
        REQUIRE(fixture.history.samples[1].pathSpeed == doctest::Approx(3.0f));
        REQUIRE(fixture.recorder.haveDistinct);
    };

    SUBCASE("generation boundary: the first sample in the new world reads 0") {
        auto fixture = std::make_unique<Fixture>();
        seed(*fixture);

        clearHistory(fixture->history);
        clearRecorder(fixture->recorder);
        clearSpeedWindow(fixture->recorder);
        CHECK_FALSE(fixture->recorder.haveDistinct);

        const RecordReport report = recordSample(
            fixture->recorder, fixture->history, at(1000.0f, 0.0f, 0.0f, 100));
        CHECK(report.result == RecordResult::Recorded);
        CHECK(report.pathSpeed == doctest::Approx(0.0f));
        CHECK(fixture->recorder.latestRecordedSpeed == doctest::Approx(0.0f));
        CHECK(fixture->recorder.lastDistinctTick == 100);
    }

    SUBCASE("without the reseed the same warp reports a whole-scene speed") {
        auto fixture = std::make_unique<Fixture>();
        seed(*fixture);
        clearHistory(fixture->history);
        clearRecorder(fixture->recorder);

        const RecordReport report = recordSample(
            fixture->recorder, fixture->history, at(1000.0f, 0.0f, 0.0f, 100));
        CHECK(report.pathSpeed > 100.0f);
    }

    SUBCASE("an ordinary route clear keeps the window measuring live motion") {
        auto fixture = std::make_unique<Fixture>();
        seed(*fixture);
        clearHistory(fixture->history);
        clearRecorder(fixture->recorder);
        CHECK(fixture->recorder.haveDistinct);

        const RecordReport report = recordSample(
            fixture->recorder, fixture->history, at(0.2f, 0.0f, 0.0f, 5));
        CHECK(report.pathSpeed == doctest::Approx(3.0f));
    }
}

TEST_CASE("clearing the route keeps the serial counter monotonic") {
    auto fixture = std::make_unique<Fixture>();
    recordSample(fixture->recorder, fixture->history, at(0.0f, 0.0f, 0.0f, 1));
    recordSample(fixture->recorder, fixture->history, at(1.0f, 0.0f, 0.0f, 2));
    clearHistory(fixture->history);
    clearRecorder(fixture->recorder);
    CHECK(fixture->history.count == 0);
    CHECK(fixture->history.head == 0);
    CHECK_FALSE(fixture->recorder.havePreviousRecorded);

    recordSample(fixture->recorder, fixture->history, at(5.0f, 0.0f, 0.0f, 50));
    CHECK(fixture->history.samples[0].serial == 3);
    CHECK(fixture->history.samples[0].tickDelta == 1);
}
