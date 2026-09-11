#include <memory>

#include "RecallPosePlayback.hpp"
#include "RecallRouteProbePlan.hpp"
#include "doctest.h"

using namespace self_recall::pure;

namespace {
struct PlaybackFixture {
    std::unique_ptr<PoseHistorySlot[]> slots = std::make_unique<PoseHistorySlot[]>(128);
    std::unique_ptr<PosePayloadBlock[]> payload = std::make_unique<PosePayloadBlock[]>(128 * 36);
    PoseHistory history{slots.get(), 128, {payload.get(), 128 * 36}};
    GameTimeSnapshot clock{};

    void record(unsigned fps = 30, unsigned count = 61, unsigned unsafeFrame = UINT32_MAX,
                float metersPerFrame = 1.0f) {
        GameTime time;
        RecordedModelPose model{};
        model.identity = {1, 2, 3, 0, 1, 0, 0};
        RecordedBoneMatrix bone{};
        PoseFrameInput input{};
        input.models = &model;
        input.bones = &bone;
        input.header.modelCount = input.header.boneCount = 1;
        input.header.worldGeneration = input.header.modelGeneration = 1;
        for (unsigned i = 0; i < count; ++i) {
            clock = time.update(fps == 60 ? 0.5f : 1.0f, false);
            input.header.frameEpoch = clock.serial;
            input.header.elapsedNanoseconds = clock.elapsedNanoseconds;
            input.header.route.flags = i == unsafeFrame ? 0 : SampleAdmissible;
            input.header.route.pose.position.x = static_cast<float>(i) * metersPerFrame;
            bone.words[12] = i;
            REQUIRE(history.record(input).status == PoseRecordStatus::Recorded);
        }
    }
};
}

TEST_CASE("reverse cursor holds a 30 Hz pose between 60 Hz playback updates") {
    PlaybackFixture fixture;
    fixture.record();
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    const auto newest = playback.selectedKey();
    fixture.clock.elapsedNanoseconds += 16666666;
    CHECK(playback.step(fixture.clock, 1) == PosePlaybackStatus::Held);
    CHECK(playback.selectedKey() == newest);
    fixture.clock.elapsedNanoseconds += 16666667;
    CHECK(playback.step(fixture.clock, 1) == PosePlaybackStatus::Ready);
    CHECK(playback.index() == 1);
    REQUIRE(playback.selectedFrame());
    CHECK(playback.selectedFrame()->header.route.pose.position.x == 59);
    CHECK(playback.selectedFrame()->bones[0].words[12] == 59);
    CHECK(playback.selectedKey().serial == newest.serial - 1);
}

TEST_CASE("all calibration rates select movement and animation on one accelerated timeline") {
    for (const auto fps : {30u, 60u}) {
        PlaybackFixture fixture;
        fixture.record(fps, fps * 2 + 1);
        for (const auto rate : kPlaybackRates) {
            CAPTURE(fps); CAPTURE(playbackRateText(rate));
            PosePlayback playback;
            auto clock = fixture.clock;
            REQUIRE(playback.begin(fixture.history, 1, clock) == PosePlaybackStatus::Ready);
            clock.elapsedNanoseconds += 200000000;
            REQUIRE(playback.step(clock, 1, UINT32_MAX, rate) == PosePlaybackStatus::Ready);
            const auto expectedIndex = fps * static_cast<unsigned>(rate) / 20;
            CHECK(playback.elapsedNanoseconds() == 50000000ull * static_cast<unsigned>(rate));
            CHECK(playback.index() == expectedIndex);
            CHECK(playback.selectedFrame()->header.route.pose.position.x == float(fps * 2 - expectedIndex));
            CHECK(playback.selectedFrame()->bones[0].words[12] == fps * 2 - expectedIndex);
            clock.elapsedNanoseconds = UINT64_MAX;
            CHECK(playback.step(clock, 1, UINT32_MAX, rate) == PosePlaybackStatus::AtEnd);
            CHECK(playback.elapsedNanoseconds() == playback.durationNanoseconds());
            CHECK(playback.selectedFrame()->bones[0].words[12] == 0);
        }
    }
}

TEST_CASE("fractional speed changes preserve accrued time without changing the recorded history") {
    PlaybackFixture fixture;
    fixture.record();
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    const auto anchor = playback.selectedKey();
    for (unsigned i = 0; i < 4; ++i) {
        ++fixture.clock.elapsedNanoseconds;
        CHECK(playback.step(fixture.clock, 1, UINT32_MAX, PlaybackRate::X125) == PosePlaybackStatus::Held);
    }
    CHECK(playback.elapsedNanoseconds() == 5);
    fixture.clock.elapsedNanoseconds += 100000000;
    CHECK(playback.step(fixture.clock, 1, UINT32_MAX, PlaybackRate::X400) == PosePlaybackStatus::Ready);
    CHECK(playback.elapsedNanoseconds() == 400000005);
    fixture.clock.elapsedNanoseconds += 100000000;
    CHECK(playback.step(fixture.clock, 1, UINT32_MAX, PlaybackRate::X150) == PosePlaybackStatus::Ready);
    CHECK(playback.elapsedNanoseconds() == 550000005);
    const auto selected = playback.selectedKey();
    CHECK(playback.step(fixture.clock, 1, UINT32_MAX, static_cast<PlaybackRate>(0)) == PosePlaybackStatus::InvalidSpeed);
    CHECK(playback.selectedKey() == selected);
    CHECK(playback.elapsedNanoseconds() == 550000005);
    auto original = fixture.history.acquire(anchor);
    REQUIRE(original);
    CHECK(original.get()->header.route.pose.position.x == 60);
    CHECK(original.get()->bones[0].words[12] == 60);
    playback.reset();
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    CHECK(playback.elapsedNanoseconds() == 0);
}

TEST_CASE("accelerated native clock phases keep exact frame cadence at 30 and 60 Hz") {
    for (const unsigned recordFps : {30u, 60u}) {
        for (unsigned recordPhase = 0; recordPhase < 3; ++recordPhase) {
            PlaybackFixture fixture;
            const unsigned count = recordFps * 2 + 1 + recordPhase;
            fixture.record(recordFps, count);
            for (const unsigned playFps : {30u, 60u}) {
                for (unsigned phase = 0; phase < 3; ++phase) {
                    for (const auto rate : kPlaybackRates) {
                        CAPTURE(recordFps); CAPTURE(recordPhase); CAPTURE(playFps);
                        CAPTURE(phase); CAPTURE(playbackRateText(rate));
                        GameTime live;
                        for (unsigned i = 0; i < 300 + phase; ++i) fixture.clock = live.update(0.5f, false);
                        PosePlayback playback;
                        REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
                        for (unsigned tick = 1; tick <= 10; ++tick) {
                            fixture.clock = live.update(playFps == 60 ? 0.5f : 1.0f, false);
                            const auto before = playback.index();
                            const auto expected = tick * static_cast<unsigned>(rate) * recordFps / (4u * playFps);
                            CHECK(playback.step(fixture.clock, 1, UINT32_MAX, rate) ==
                                (expected == before ? PosePlaybackStatus::Held : PosePlaybackStatus::Ready));
                            CHECK(playback.index() == expected);
                            CHECK(playback.selectedFrame()->header.route.pose.position.x == float(count - expected - 1));
                            CHECK(playback.selectedFrame()->bones[0].words[12] == count - expected - 1);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("accelerated pause holds and checked boundaries do not accumulate catch-up debt") {
    PlaybackFixture fixture;
    fixture.record();
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    fixture.clock.elapsedNanoseconds += 200000000;
    REQUIRE(playback.step(fixture.clock, 1, 3, PlaybackRate::X400) == PosePlaybackStatus::Ready);
    CHECK(playback.index() == 3);
    for (const auto status : {GameTimeStatus::Paused, GameTimeStatus::NoAdvance}) {
        fixture.clock.status = status;
        fixture.clock.elapsedNanoseconds += 1000000000;
        CHECK(playback.step(fixture.clock, 1, 3, PlaybackRate::X400) == PosePlaybackStatus::Held);
        CHECK(playback.elapsedNanoseconds() == 100000000);
    }
    fixture.clock.status = GameTimeStatus::Running;
    fixture.clock.elapsedNanoseconds += 1000000000;
    REQUIRE(playback.hold(fixture.clock));
    fixture.clock.elapsedNanoseconds += 10000000;
    REQUIRE(playback.step(fixture.clock, 1, 12, PlaybackRate::X400) == PosePlaybackStatus::Ready);
    CHECK(playback.elapsedNanoseconds() == 140000000);
    CHECK(playback.index() == 4);
}

TEST_CASE("4x cannot skip an unsafe pose even when its destination and checked prefix are safe") {
    PlaybackFixture fixture;
    fixture.record(60, 121, 118);
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    const auto before = playback.selectedKey();
    fixture.clock.elapsedNanoseconds += 33333334;
    CHECK(playback.step(fixture.clock, 1, 12, PlaybackRate::X400) == PosePlaybackStatus::UnsafeSample);
    CHECK(playback.selectedKey() == before);
}

TEST_CASE("one-window prefetch sustains 4x across asynchronous route checks") {
    PlaybackFixture fixture;
    fixture.record(60, 121, UINT32_MAX, 0.1f); //12 frames =1.2m, within native3m bound
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    std::uint32_t checked = 0, pending = 0;
    const auto arm = [&] {
        const auto start = nextRouteProbeStart(playback.index(), checked, playback.count(), true);
        if (start == UINT32_MAX) return;
        std::array<HistorySample, kProbeWindowMaxSamples + 1> route{};
        unsigned count = 0;
        for (; count < route.size() && start + count < playback.count(); ++count)
            route[count] = playback.frameAt(start + count).get()->header.route;
        const auto plan = planRouteProbe({route.data(), count}, false);
        REQUIRE(plan.kind == RouteProbeKind::Cast);
        REQUIRE(plan.through > 0);
        pending = start + plan.through;
    };
    arm();
    checked = pending; pending = 0; // initial route admission
    for (unsigned tick = 1; tick <= 15; ++tick) {
        if (pending) { checked = pending; pending = 0; } // previous native worker completes
        arm();
        fixture.clock.elapsedNanoseconds += tick % 3 == 0 ? 33333334 : 33333333;
        const auto result = playback.step(fixture.clock, 1, checked, PlaybackRate::X400);
        CHECK(result == (tick == 15 ? PosePlaybackStatus::AtEnd : PosePlaybackStatus::Ready));
        CHECK(playback.index() == tick * 8);
        CHECK(playback.selectedFrame()->bones[0].words[12] == 120 - tick * 8);
        if (!pending) arm();
    }
}

TEST_CASE("prefetch stays bounded and a failed next window cannot grant unchecked frames") {
    CHECK(nextRouteProbeStart(0, 12, 61, false) == UINT32_MAX);
    CHECK(nextRouteProbeStart(0, 12, 61, true) == 12);
    CHECK(nextRouteProbeStart(0, 24, 61, true) == UINT32_MAX);
    CHECK(nextRouteProbeStart(12, 12, 61, false) == 12);
    CHECK(nextRouteProbeStart(50, 60, 61, true) == UINT32_MAX);
    CHECK(nextRouteProbeStart(0, 0, 0, true) == UINT32_MAX);
    PlaybackFixture fixture;
    fixture.record();
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    fixture.clock.elapsedNanoseconds += 50000000;
    CHECK(playback.step(fixture.clock, 1, 12, PlaybackRate::X400) == PosePlaybackStatus::Ready);
    CHECK(playback.index() == 6); // failure does not discard the already safe prefix
    fixture.clock.elapsedNanoseconds += 1000000000;
    CHECK(playback.step(fixture.clock, 1, 12, PlaybackRate::X400) == PosePlaybackStatus::Ready);
    CHECK(playback.index() == 12);
    fixture.clock.elapsedNanoseconds += 1000000000;
    CHECK(playback.step(fixture.clock, 1, 12, PlaybackRate::X400) == PosePlaybackStatus::Held);
    CHECK(playback.index() == 12);
}

TEST_CASE("delayed reverse updates cannot skip an unsafe recorded frame") {
    PlaybackFixture fixture;
    fixture.record(30, 61, 58);
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    const auto newest = playback.selectedKey();
    fixture.clock.elapsedNanoseconds += 1000000000;
    CHECK(playback.step(fixture.clock, 1) == PosePlaybackStatus::UnsafeSample);
    CHECK(playback.selectedKey() == newest);
    CHECK(playback.index() == 0);
}

TEST_CASE("pause and collision holds do not cause reverse catch-up") {
    PlaybackFixture fixture;
    fixture.record();
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    fixture.clock.status = GameTimeStatus::Paused;
    CHECK(playback.step(fixture.clock, 1) == PosePlaybackStatus::Held);
    fixture.clock.status = GameTimeStatus::Running;
    fixture.clock.elapsedNanoseconds += 500000000;
    REQUIRE(playback.hold(fixture.clock));
    CHECK(playback.elapsedNanoseconds() == 0);
    fixture.clock.elapsedNanoseconds += 33333333;
    CHECK(playback.step(fixture.clock, 1) == PosePlaybackStatus::Ready);
    CHECK(playback.index() == 1);
}

TEST_CASE("reverse cursor finishes at the oldest frame and rejects invalidation") {
    PlaybackFixture fixture;
    fixture.record(60, 121);
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    CHECK(playback.durationNanoseconds() == 2000000000ull);
    fixture.clock.elapsedNanoseconds += 10000000000ull;
    CHECK(playback.step(fixture.clock, 1) == PosePlaybackStatus::AtEnd);
    CHECK(playback.index() == 120);
    CHECK(playback.selectedFrame()->header.route.pose.position.x == 0);
    CHECK(playback.selectedFrame()->bones[0].words[12] == 0);
    CHECK(playback.step(fixture.clock, 2) == PosePlaybackStatus::WrongWorld);
    fixture.history.clear();
    CHECK(playback.step(fixture.clock, 1) == PosePlaybackStatus::UnavailableFrame);
    playback.reset();
    CHECK_FALSE(playback.selectedKey());
}

TEST_CASE("reverse admission requires elapsed history and a current gameplay clock") {
    PlaybackFixture fixture;
    fixture.record(60, 60);
    PosePlayback playback;
    CHECK(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::TooShort);
    fixture.clock.status = GameTimeStatus::Paused;
    CHECK(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::InvalidClock);
    fixture.clock.status = GameTimeStatus::Running;
    CHECK(playback.begin(fixture.history, 2, fixture.clock) == PosePlaybackStatus::WrongWorld);
}

TEST_CASE("late clock updates stop at the checked route boundary without accumulating debt") {
    PlaybackFixture fixture;
    fixture.record();
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    fixture.clock.elapsedNanoseconds += 1000000000;
    CHECK(playback.step(fixture.clock, 1, 3) == PosePlaybackStatus::Ready);
    CHECK(playback.index() == 3);
    CHECK(playback.selectedFrame()->bones[0].words[12] == 57);
    fixture.clock.elapsedNanoseconds += 1000000000;
    CHECK(playback.step(fixture.clock, 1, 3) == PosePlaybackStatus::Held);
    CHECK(playback.index() == 3);
    fixture.clock.elapsedNanoseconds += 33333334;
    CHECK(playback.step(fixture.clock, 1, 12) == PosePlaybackStatus::Ready);
    CHECK(playback.index() == 4);
    CHECK(playback.elapsedNanoseconds() < 134000000);
    CHECK(playback.step(fixture.clock, 1, 3) == PosePlaybackStatus::UnavailableFrame);
}

TEST_CASE("60 Hz recorded animation advances two frames per 30 Hz playback update") {
    PlaybackFixture fixture;
    fixture.record(60, 121);
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    for (unsigned i = 1; i <= 30; ++i) {
        fixture.clock.elapsedNanoseconds += i % 3 == 0 ? 33333334 : 33333333;
        CHECK(playback.step(fixture.clock, 1) == PosePlaybackStatus::Ready);
        CHECK(playback.index() == 2 * i);
        CHECK(playback.selectedFrame()->bones[0].words[12] == 120 - 2 * i);
    }
}

TEST_CASE("native clock rounding never repeats or skips frames at matching playback rates") {
    for (const unsigned fps : {30u, 60u}) {
        for (unsigned recordPhase = 0; recordPhase < 3; ++recordPhase) {
            PlaybackFixture fixture;
            const unsigned count = fps * 2 + 1 + recordPhase;
            fixture.record(fps, count);
            for (unsigned playbackPhase = 0; playbackPhase < 3; ++playbackPhase) {
                CAPTURE(fps);
                CAPTURE(recordPhase);
                CAPTURE(playbackPhase);
                GameTime live;
                for (unsigned i = 0; i < 300 + playbackPhase; ++i)
                    fixture.clock = live.update(0.5f, false);
                PosePlayback playback;
                REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
                for (unsigned i = 1; i < count; ++i) {
                    fixture.clock = live.update(fps == 60 ? 0.5f : 1.0f, false);
                    const auto status = playback.step(fixture.clock, 1);
                    CHECK(status == (i + 1 == count ? PosePlaybackStatus::AtEnd : PosePlaybackStatus::Ready));
                    CHECK(playback.index() == i);
                    CHECK(playback.selectedFrame()->bones[0].words[12] == count - i - 1);
                }
            }
        }
    }
}

TEST_CASE("fractional rates produce even positions without changing the recorded animation key") {
    for (const auto rate : kPlaybackRates) {
        PlaybackFixture fixture;
        fixture.record(30, 121, UINT32_MAX, 5.0f);
        PosePlayback playback;
        REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
        auto clock = fixture.clock;
        const float stride = float(static_cast<unsigned>(rate)) * 1.25f;
        float previous = playback.appliedSample().pose.position.x;
        for (unsigned tick = 1; tick <= 24; ++tick) {
            clock.elapsedNanoseconds = fixture.clock.elapsedNanoseconds + std::uint64_t(tick) * 100000000 / 3;
            REQUIRE(playback.step(clock, 1, UINT32_MAX, rate) == PosePlaybackStatus::Ready);
            const auto shown = playback.appliedSample();
            CHECK(previous - shown.pose.position.x == doctest::Approx(stride).epsilon(0.0001));
            CHECK(playback.presentation().key == playback.selectedKey());
            CHECK(playback.selectedFrame()->header.route.pose.position.x ==
                  float(120 - playback.index()) * 5.0f);
            previous = shown.pose.position.x;
        }
    }
}

TEST_CASE("fractional positions stop at the checked boundary and never approach unsafe history") {
    PlaybackFixture fixture;
    fixture.record(30, 61, 58);
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    auto clock = fixture.clock;
    clock.elapsedNanoseconds += 33333333;
    REQUIRE(playback.step(clock, 1, 1, static_cast<PlaybackRate>(7)) == PosePlaybackStatus::Ready);
    CHECK(playback.appliedSample().pose.position.x == 59);
    CHECK(playback.presentation().offset.x == 0);
    clock.elapsedNanoseconds += 33333333;
    CHECK(playback.step(clock, 1, 2, static_cast<PlaybackRate>(7)) == PosePlaybackStatus::UnsafeSample);
}
