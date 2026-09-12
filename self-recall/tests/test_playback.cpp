#include <memory>
#include "PoseTestSupport.hpp"
#include "RecallPlayback.hpp"
#include "doctest.h"
#include <array>
#include <limits>
#include "RecallBase.hpp"
#include "RecallModelEngine.hpp"
#include "RecallRender.hpp"

namespace self_recall_tests::test_recall_pose_playback {
using namespace self_recall::pure;

namespace {
struct PlaybackFixture {
    PoseTestStorage storage{128, 128 * 36};
    PoseHistory& history = storage.history;
    GameTimeSnapshot clock{};

    void record(unsigned fps = 30, unsigned count = 61, unsigned unsafeFrame = UINT32_MAX,
                float metersPerFrame = 1.0f) {
        GameTime time;
        PoseTestInput source;
        source.models[0].identity = {1, 2, 3, 0, 1, 0, 0};
        auto& input = source.value;
        for (unsigned i = 0; i < count; ++i) {
            clock = time.update(fps == 60 ? 0.5f : 1.0f, false);
            input.header.frameEpoch = clock.serial;
            input.header.elapsedNanoseconds = clock.elapsedNanoseconds;
            input.header.route.flags = i == unsafeFrame ? 0 : SampleAdmissible;
            input.header.route.pose.position.x = static_cast<float>(i) * metersPerFrame;
            source.bones[0].words[12] = i;
            REQUIRE(history.record(input).status == PoseRecordStatus::Recorded);
        }
    }
};
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
                        const bool fullRun = rate == PlaybackRate::Normal && recordFps == playFps;
                        const unsigned ticks = fullRun ? count - 1 :
                            rate == PlaybackRate::Normal && recordFps == 60 && playFps == 30 ? 30 : 10;
                        for (unsigned tick = 1; tick <= ticks; ++tick) {
                            fixture.clock = live.update(playFps == 60 ? 0.5f : 1.0f, false);
                            const auto before = playback.index();
                            const auto expected = tick * static_cast<unsigned>(rate) * recordFps / (4u * playFps);
                            const auto status = expected + 1 == count ? PosePlaybackStatus::AtEnd :
                                expected == before ? PosePlaybackStatus::Held : PosePlaybackStatus::Ready;
                            CHECK(playback.step(fixture.clock, 1, UINT32_MAX, rate) == status);
                            CHECK(playback.index() == expected);
                            CHECK(playback.selectedKey().serial == count - expected);
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
    const auto heldAt = playback.elapsedNanoseconds();
    REQUIRE(playback.hold(fixture.clock));
    CHECK(playback.elapsedNanoseconds() == heldAt);
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
    fixture.record(60, 121, UINT32_MAX, 0.1f);
    PosePlayback playback;
    REQUIRE(playback.begin(fixture.history, 1, fixture.clock) == PosePlaybackStatus::Ready);
    std::uint32_t checked = 0, pending = 0;
    const auto arm = [&] {
        const auto start = nextRouteProbeStart(playback.index(), checked, playback.count(), true);
        if (start == UINT32_MAX) return;
        std::array<HistorySample, kProbeWindowMaxSamples + 1> route{};
        unsigned count = 0;
        for (; count < route.size() && start + count < playback.count(); ++count) {
            PoseFrameHeader frame;
            REQUIRE(playback.headerAt(start + count, frame));
            route[count] = frame.route;
        }
        const auto plan = planRouteProbe({route.data(), count}, false);
        REQUIRE(plan.kind == RouteProbeKind::Cast);
        REQUIRE(plan.through > 0);
        pending = start + plan.through;
    };
    arm();
    checked = pending; pending = 0;
    for (unsigned tick = 1; tick <= 15; ++tick) {
        if (pending) { checked = pending; pending = 0; }
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
    CHECK(playback.index() == 6);
    fixture.clock.elapsedNanoseconds += 1000000000;
    CHECK(playback.step(fixture.clock, 1, 12, PlaybackRate::X400) == PosePlaybackStatus::Ready);
    CHECK(playback.index() == 12);
    fixture.clock.elapsedNanoseconds += 1000000000;
    CHECK(playback.step(fixture.clock, 1, 12, PlaybackRate::X400) == PosePlaybackStatus::Held);
    CHECK(playback.index() == 12);
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
}

namespace self_recall_tests::test_recall_route_probe_plan {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

using namespace self_recall::pure;

TEST_CASE("actor-frame native climb admits a leaned route after the perimeter observation drops") {
    std::array<HistorySample, 2> samples{};
    for (auto& sample : samples) {
        sample.pose.rotation.values[0] = sample.pose.rotation.values[8] = 1;
        sample.pose.rotation.values[4] = 0.2f;
    }
    samples[1].pose.position.y = 0.2f;
    CHECK(planRouteProbe(samples, false).kind == RouteProbeKind::Unavailable);
    for (auto& sample : samples)
        sample.flags = pairNativeClimbAdmission(sample.flags, true, true);
    CHECK(planRouteProbe(samples, false).kind == RouteProbeKind::Climb);
    CHECK(planRouteProbe(samples, false).through == 1);
    samples[1].flags = pairNativeClimbAdmission(0, false, true);
    CHECK(planRouteProbe(samples, true).kind == RouteProbeKind::Unavailable);
    samples[1].flags = pairNativeClimbAdmission(0, true, false);
    CHECK(planRouteProbe(samples, true).kind == RouteProbeKind::Unavailable);
    CHECK((pairNativeClimbAdmission(SampleNativeGlide, true, true) & SampleNativeGlide) != 0);
}

namespace {
auto route(float spacing = 0.25f) {
    std::array<HistorySample, 16> samples{};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i].flags = SampleAdmissible;
        samples[i].pose.rotation.values[0] = samples[i].pose.rotation.values[4] = samples[i].pose.rotation.values[8] = 1;
        samples[i].pose.position.x = static_cast<float>(i) * spacing;
    }
    return samples;
}
}

TEST_CASE("route probe includes the current pose and bounds checked coverage") {
    const auto samples = route(0.5f);
    const auto plan = planRouteProbe(samples, false);
    CHECK(plan.kind == RouteProbeKind::Cast);
    REQUIRE(plan.count == 6);
    CHECK(plan.segments[0].from.x == 0);
    CHECK(plan.segments[plan.count - 1].to.x == 3);
    CHECK(plan.through == 6);
    CHECK(planRouteProbe(route(0.1f), false).through == 12);
    CHECK(planRouteProbe(std::span(samples).first(2), false).through == 1);
}

TEST_CASE("route check distinguishes stationary and steep policy bypasses") {
    auto samples = route(0);
    CHECK(planRouteProbe(samples, false).kind == RouteProbeKind::Stationary);
    CHECK(planRouteProbe(samples, false).through == 12);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i].pose.position.y = static_cast<float>(i);
    CHECK(planRouteProbe(samples, false).kind == RouteProbeKind::Steep);
    CHECK(planRouteProbe(samples, false).through == 3);
}

TEST_CASE("climb route exemption ends at the first ordinary segment") {
    auto samples = route();
    samples[0].flags |= SampleClimb;
    samples[1].flags |= SampleClimb;
    const auto plan = planRouteProbe(samples, false);
    CHECK(plan.kind == RouteProbeKind::Climb);
    CHECK(plan.through == 2);
    CHECK(planRouteProbe(std::span(samples).subspan(2), false).kind == RouteProbeKind::Cast);
    CHECK(planRouteProbe(samples, true).through == 12);
}

TEST_CASE("route probes cannot grant coverage through unsafe or corrupt samples") {
    auto samples = route();
    samples[4].flags = 0;
    CHECK(planRouteProbe(samples, false).through == 3);
    samples[1].pose.position.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(planRouteProbe(samples, false).kind == RouteProbeKind::Unavailable);
    CHECK(planRouteProbe({}, false).kind == RouteProbeKind::Unavailable);
    CHECK(planRouteProbe(std::span(samples).first(1), false).kind == RouteProbeKind::Unavailable);
}

TEST_CASE("control-stick route contact exemption ends when ordinary history resumes") {
    auto samples = route();
    samples[0].flags |= SampleControlStick;
    samples[1].flags |= SampleControlStick;
    const auto plan = planRouteProbe(samples, false);
    CHECK(plan.kind == RouteProbeKind::Vehicle);
    CHECK(plan.through == 2);
    CHECK(planRouteProbe(std::span(samples).subspan(2), false).kind == RouteProbeKind::Cast);
    samples[1].flags = SampleControlStick;
    CHECK(planRouteProbe(samples, false).kind == RouteProbeKind::Unavailable);
}

TEST_CASE("route contact tolerance preserves interior walls and rejects invalid geometry") {
    RouteProbeSegment segment{{0, 0, 0}, {0, -0.06f, 0}};
    CHECK(tolerableRouteContact(segment, {0, -0.058f, 0}, {-0.134f, 0.987f, 0.092f}));
    CHECK_FALSE(tolerableRouteContact(segment, {0, -0.03f, 0}, {0, 1, 0}));
    segment.to = {0.01f, 0.04f, 0.03f};
    CHECK(tolerableRouteContact(segment, {}, {0.182f, -0.760f, -0.623f}));
    segment.to = {1, 0, 0};
    CHECK_FALSE(tolerableRouteContact(segment, {0.01f, 0, 0}, {-1, 0, 0}));
    CHECK_FALSE(tolerableRouteContact(segment, {}, {-1, 0, 0}));
    CHECK(tolerableRouteContact(segment, {}, {1, 0, 0}));
    CHECK_FALSE(tolerableRouteContact(segment, {}, {}));
    CHECK_FALSE(tolerableRouteContact(segment, {}, {std::numeric_limits<float>::quiet_NaN(), 0, 0}));
}

TEST_CASE("obstacle rays follow the recorded bend instead of cutting a corner") {
    auto samples = route();
    samples[0].pose.position = {0, 0, 0};
    samples[1].pose.position = {1, 0, 0};
    samples[2].pose.position = {1, 0, 1};
    const auto plan = planRouteProbe(std::span(samples).first(3), false);
    REQUIRE(plan.kind == RouteProbeKind::Cast);
    REQUIRE(plan.count == 2);
    CHECK(plan.segments[0].from.z == 0);
    CHECK(plan.segments[0].to.z == 0);
    CHECK(plan.segments[1].from.x == 1);
    CHECK(plan.segments[1].to.x == 1);
    CHECK(plan.segments[0].to.y == doctest::Approx(kProbeLiftMeters));
    samples[2].pose.position = samples[0].pose.position;
    CHECK(planRouteProbe(std::span(samples).first(3), false).kind == RouteProbeKind::Cast);
}

TEST_CASE("probe lift follows recorded local up through a leaned ledge transition") {
    auto samples = route();
    const float rotation[]{0, -1, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(samples[1].pose.rotation.values, rotation, sizeof(rotation));
    const auto plan = planRouteProbe(std::span(samples).first(2), false);
    REQUIRE(plan.count == 1);
    CHECK(plan.segments[0].from.y == doctest::Approx(kProbeLiftMeters));
    CHECK(plan.segments[0].to.y == 0);
    CHECK(plan.segments[0].to.x == doctest::Approx(0.25f - kProbeLiftMeters));
}

namespace {
unsigned castCalls;
int hitCall;
std::uint64_t testCast(const void*, const void*, const void* object, const void*, std::uint32_t, std::uint32_t) {
    auto* query = const_cast<unsigned char*>(static_cast<const unsigned char*>(object));
    CHECK(query[totk::engine::layout::kRaycastHit] == 0);
    query[totk::engine::layout::kRaycastHit] = static_cast<int>(castCalls++) == hitCall;
    return 0;
}
std::uint64_t contactThenWall(const void* from, const void*, const void* object, const void*, std::uint32_t, std::uint32_t) {
    auto* query = const_cast<unsigned char*>(static_cast<const unsigned char*>(object));
    const auto index = castCalls++;
    if (index > 1) return 0;
    auto hit = *static_cast<const totk::core::WorldPosition*>(from);
    const totk::core::WorldPosition normal{index == 0 ? 1.0f : -1.0f, 0, 0};
    if (index == 1) hit.x += 0.125f;
    query[totk::engine::layout::kRaycastHit] = 1;
    std::memcpy(query + totk::engine::layout::kRaycastPosition, &hit, sizeof(hit));
    std::memcpy(query + totk::engine::layout::kRaycastNormal, &normal, sizeof(normal));
    return 0;
}
}

TEST_CASE("tolerating a first contact still checks later segments for a wall") {
    self_recall::probe::RouteProbeBatch batch;
    const auto plan = planRouteProbe(route(), false);
    std::array<unsigned char, totk::engine::layout::kRaycastObjectSize> query{};
    totk::core::WorldPosition near{};
    REQUIRE(batch.begin(plan, 0x20, 10, 2));
    castCalls = 0;
    batch.observe(contactThenWall, &near, query.data());
    const auto result = batch.poll(11, 24);
    CHECK(result.status == totk::engine::RaycastPollStatus::Ready);
    CHECK(result.hit.hit);
    CHECK(result.segment == 1);
}

TEST_CASE("route batch checks every segment in one physics callback and preserves obstacle stops") {
    using self_recall::probe::RouteProbeBatch;
    using Status = totk::engine::RaycastPollStatus;
    RouteProbeBatch batch;
    const auto plan = planRouteProbe(route(), false);
    std::array<unsigned char, totk::engine::layout::kRaycastObjectSize> query{};
    query[totk::engine::layout::kRaycastHit] = 1;
    totk::core::WorldPosition near{}, far{100, 100, 100};
    REQUIRE(batch.begin(plan, 0x20, 10, 2));
    castCalls = 0; hitCall = -1;
    batch.observe(testCast, &far, query.data());
    CHECK(castCalls == 0);
    CHECK(batch.poll(11, 24).status == Status::Pending);
    batch.observe(testCast, &near, query.data());
    CHECK(castCalls == plan.count);
    const auto clear = batch.poll(12, 24);
    CHECK(clear.status == Status::Ready);
    CHECK_FALSE(clear.hit.hit);
    REQUIRE(batch.begin(plan, 0x20, 13, 2));
    castCalls = 0; hitCall = 2;
    batch.observe(testCast, &near, query.data());
    const auto blocked = batch.poll(14, 24);
    CHECK(blocked.status == Status::Ready);
    CHECK(blocked.hit.hit);
    CHECK(blocked.segment == 2);
    REQUIRE(batch.begin(plan, 0x20, 15, 3));
    CHECK(batch.poll(40, 24).status == Status::TimedOut);
    REQUIRE(batch.begin(plan, 0x20, 41, 4));
    batch.cancel();
    castCalls = 0;
    batch.observe(testCast, &near, query.data());
    CHECK(castCalls == 0);
    CHECK(batch.poll(42, 24).status == Status::Idle);
}
}

namespace self_recall_tests::test_recall_native_path {
using namespace self_recall::pure;

namespace {
NativePathRoute route() {
    NativePathRoute result;
    result.anchor = {105, 7, 5};
    result.world = 2;
    result.historyCount = 6;
    result.newestNanoseconds = 1000;
    result.oldestNanoseconds = 700;
    result.route.count = 4;
    result.route.points[0] = {{5, 1, 0}, 5};
    result.route.points[1] = {{3, 8, 0}, 3};
    result.route.points[2] = {{1, 2, 0}, 1};
    result.route.points[3] = {{0, 0, 0}, 0};
    result.phases[0] = 0;
    result.phases[1] = 1.0f / 6;
    result.phases[2] = 2.0f / 3;
    result.phases[3] = 1;
    return result;
}
PoseFrameHeader selected() {
    PoseFrameHeader result;
    result.key = {103, 7, 3};
    result.worldGeneration = 2;
    result.elapsedNanoseconds = 950;
    result.route.flags = SampleAdmissible;
    result.route.pose.position = {3, 8, 0};
    return result;
}
}

TEST_CASE("native ribbon trims against the latched animation key and begins at its recorded root") {
    const auto source = route();
    auto header = selected();
    NativePathFrame out;
    REQUIRE(buildNativePathFrame(out, source, header, 50) == NativePathStatus::Ready);
    CHECK(out.key == header.key);
    REQUIRE(out.count == 3);
    CHECK(out.points[0].position.y == 8);
    CHECK(out.points[0].phase == doctest::Approx(1.0 / 6));
    CHECK(out.points[1].position.x == 1);
    CHECK(out.points[1].phase == doctest::Approx(2.0 / 3));
    CHECK(out.points[2].phase == 1);
    header.key.serial = 102;
    header.elapsedNanoseconds = 900;
    header.route.pose.position = {2, 4, 1};
    REQUIRE(buildNativePathFrame(out, source, header, 51) == NativePathStatus::Ready);
    CHECK(out.points[0].position.z == 1);
    CHECK(out.points[0].phase == doctest::Approx(1.0 / 3));
    header.key.serial = 100;
    header.elapsedNanoseconds = 700;
    header.route.pose.position = {0, 0, 0};
    CHECK(buildNativePathFrame(out, source, header, 52) == NativePathStatus::Empty);
    CHECK(out.count == 1);
}

TEST_CASE("native ribbon rejects stale routes and malformed geometry without publishing a partial path") {
    auto source = route();
    auto header = selected();
    NativePathFrame out;
    SUBCASE("different history generation") { ++header.key.generation; }
    SUBCASE("different world") { ++header.worldGeneration; }
    SUBCASE("future key") { header.key.serial = 106; }
    SUBCASE("expired key") { header.key.serial = 99; }
    SUBCASE("bad capacity") { source.route.count = kMaxRenderPoints + 1; }
    SUBCASE("unordered vertex") { source.route.points[2].ordinal = 4; }
    SUBCASE("NaN vertex") { source.route.points[2].position.z = std::numeric_limits<float>::quiet_NaN(); }
    SUBCASE("infinite root") { header.route.pose.position.x = std::numeric_limits<float>::infinity(); }
    SUBCASE("unsafe frame") { header.route.flags = 0; }
    SUBCASE("expired time") { header.elapsedNanoseconds = 699; }
    SUBCASE("key and timestamp disagree") { header.elapsedNanoseconds = 750; }
    SUBCASE("reversed UV time") { source.phases[2] = 0; }
    SUBCASE("invalid UV time") { source.phases[2] = std::numeric_limits<float>::infinity(); }
    const auto status = buildNativePathFrame(out, source, header, 50);
    CHECK(status != NativePathStatus::Ready);
    CHECK(status != NativePathStatus::Empty);
    CHECK(out.count == 0);
    CHECK_FALSE(out.key);
}

TEST_CASE("swimming ribbon uses the recorded water surface while the player keeps the submerged root") {
    auto header = selected();
    header.haveWaterHeight = true;
    header.waterHeight = 9;
    const auto original = header.route.pose.position;
    CHECK(recallTrailPosition(header).y == doctest::Approx(9.03f));
    NativePathFrame output;
    REQUIRE(buildNativePathFrame(output, route(), header, 51) == NativePathStatus::Ready);
    CHECK(output.points[0].position.y == doctest::Approx(9.03f));
    CHECK(header.route.pose.position.y == original.y);
    header.haveWaterHeight = false;
    CHECK(recallTrailPosition(header).y == 8);
    header.haveWaterHeight = true;
    header.waterHeight = 7;
    CHECK(recallTrailPosition(header).y == 8);
    header.waterHeight = 12;
    CHECK(recallTrailPosition(header).y == 8);
    header.waterHeight = std::numeric_limits<float>::quiet_NaN();
    CHECK(recallTrailPosition(header).y == 8);
}

TEST_CASE("stationary historical animation needs no artificial ribbon segment") {
    auto source = route();
    auto header = selected();
    for (unsigned i = 0; i < source.route.count; ++i) source.route.points[i].position = {3, 8, 0};
    NativePathFrame out;
    CHECK(buildNativePathFrame(out, source, header, 50) == NativePathStatus::Empty);
    CHECK(out.count == 1);
}

TEST_CASE("native path reads the requested render epoch without requiring a wrist or borrowing the playback cursor") {
    auto recorded = std::make_unique<RecordedPoseFrame>();
    auto store = std::make_unique<RenderFrameStore>();
    recorded->header = selected();
    recorded->header.modelCount = 1;
    REQUIRE(store->begin(*recorded, 101) == RenderPrepareStatus::Ready);
    --recorded->header.key.serial;
    recorded->header.route.pose.position.y = 4;
    REQUIRE(store->begin(*recorded, 102) == RenderPrepareStatus::Ready);
    PoseFrameHeader header;
    REQUIRE(store->copyHeader(101, 7, header));
    CHECK(header.key.serial == 103);
    CHECK(header.route.pose.position.y == 8);
    REQUIRE(store->copyHeader(102, 7, header));
    CHECK(header.key.serial == 102);
    CHECK(header.route.pose.position.y == 4);
    CHECK_FALSE(store->copyHeader(102, 8, header));
    CHECK_FALSE(store->copyHeader(103, 7, header));
}
}
