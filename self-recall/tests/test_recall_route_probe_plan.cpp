#include <array>
#include <limits>

#include "RecallRouteProbePlan.hpp"
#include "RecallGliderPolicy.hpp"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // The native query buffer deliberately has 16-byte alignment.
#endif
#include "RecallRouteProbeBatch.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "doctest.h"

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
    CHECK(plan.from.x == 0);
    CHECK(plan.to.x == 3);
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
    samples[1].flags = SampleControlStick; // Invalid state cannot be rescued by a seat flag.
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
    query[totk::engine::layout::kRaycastHit] = 1; // Never inherit a template hit.
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
