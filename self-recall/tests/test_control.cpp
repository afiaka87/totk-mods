#include <array>
#include <cmath>
#include <limits>
#include <ostream>
#include <string_view>
#include <vector>

#include <doctest.h>

#include "RecallBase.hpp"
#include "RecallPlayback.hpp"
#include "RecallVisual.hpp"

namespace self_recall_tests::test_recall_logic {
using namespace self_recall::pure;

TEST_CASE("activation owns only right-stick click while ZL and R3 are held") {
    CHECK_FALSE(activationHeld(0));
    CHECK_FALSE(activationHeld(kButtonZL));
    CHECK_FALSE(activationHeld(kButtonRightStick));
    CHECK(activationHeld(kActivationButtons));
    CHECK(activationOwnedButtons(kButtonZL) == 0);
    CHECK(activationOwnedButtons(kButtonRightStick) == 0);
    CHECK(activationOwnedButtons(kActivationButtons) == kButtonRightStick);
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

}

namespace self_recall_tests::test_recall_visual {
using namespace self_recall::pure;

namespace {

struct FedSample {
    Vec3 position{};
    std::uint8_t stateBits = 0;
};

SimplifyResult buildFromRecording(const std::vector<FedSample>& samples,
                                  RenderRoute& out) {
    return buildRoute(out, [&](RouteBuilder& builder) {
        const std::size_t n = samples.size();
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t newestFirst = n - 1 - i;
            routeFeed(builder, samples[newestFirst].position,
                      static_cast<std::uint16_t>(newestFirst),
                      samples[newestFirst].stateBits);
        }
    });
}

std::vector<FedSample> straightWalk(int count, float step) {
    std::vector<FedSample> samples;
    samples.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        samples.push_back(
            {{static_cast<float>(i) * step, 10.0f, 0.0f}, 0});
    }
    return samples;
}

bool ordinalsStrictlyDescending(const RenderRoute& route) {
    for (std::uint16_t i = 1; i < route.count; ++i) {
        if (route.points[i].ordinal >= route.points[i - 1].ordinal)
            return false;
    }
    return true;
}

}

TEST_CASE("simplification preserves the newest and oldest endpoints") {
    std::vector<FedSample> samples;
    for (int i = 0; i < 300; ++i) {
        const FedSample sample{
            {static_cast<float>(i) * 0.15f, 20.0f, 3.0f}, 0};
        samples.push_back(sample);
        samples.push_back(sample);
    }
    RenderRoute route{};
    const SimplifyResult result = buildFromRecording(samples, route);

    REQUIRE(route.count >= 2);
    CHECK(route.points[0].ordinal == samples.size() - 1);
    CHECK(route.points[0].position.x ==
          doctest::Approx(samples.back().position.x));
    CHECK(route.points[route.count - 1].ordinal == 0);
    CHECK(route.points[route.count - 1].position.x ==
          doctest::Approx(samples.front().position.x));
    CHECK(ordinalsStrictlyDescending(route));
    CHECK(result.duplicates > 0);
    CHECK(result.truncated == 0);
}

TEST_CASE("turns, vertical changes, and state transitions survive") {
    std::vector<FedSample> samples;
    for (int i = 0; i < 120; ++i)
        samples.push_back({{static_cast<float>(i) * 0.1f, 5.0f, 0.0f}, 0});
    for (int i = 1; i <= 120; ++i)
        samples.push_back({{12.0f, 5.0f, static_cast<float>(i) * 0.1f}, 0});
    for (int i = 1; i <= 80; ++i)
        samples.push_back(
            {{12.0f, 5.0f + static_cast<float>(i) * 0.1f, 12.0f}, 1});

    RenderRoute route{};
    buildFromRecording(samples, route);
    REQUIRE(route.count >= 4);
    const auto near = [](Vec3 a, Vec3 b, float threshold) {
        const auto x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
        return x * x + y * y + z * z < threshold * threshold;
    };

    bool cornerKept = false;
    for (std::uint16_t i = 0; i < route.count; ++i) {
        if (near(route.points[i].position, {12.0f, 5.0f, 0.0f},
                 kDefaultSimplifyParams.straightSpacingMeters))
            cornerKept = true;
    }
    CHECK(cornerKept);

    bool transitionKept = false;
    for (std::uint16_t i = 0; i < route.count; ++i) {
        if (near(route.points[i].position, {12.0f, 5.1f, 12.0f}, 0.75f))
            transitionKept = true;
    }
    CHECK(transitionKept);

    int climbKeeps = 0;
    for (std::uint16_t i = 0; i < route.count; ++i) {
        if (route.points[i].position.y > 5.5f &&
            route.points[i].position.y < 12.5f)
            ++climbKeeps;
    }
    CHECK(climbKeeps >= 3);
}

TEST_CASE("render-point count never exceeds its cap") {
    std::vector<FedSample> samples;
    for (int i = 0; i < 3840; ++i) {
        const float x = static_cast<float>(i) * 0.35f;
        const float z = (i % 2 == 0) ? 0.0f : 1.4f;
        samples.push_back({{x, 50.0f, z}, 0});
    }
    RenderRoute route{};
    const SimplifyResult result = buildFromRecording(samples, route);
    CHECK(route.count <= kMaxRenderPoints);
    CHECK(route.count >= 2);
    CHECK(route.points[0].ordinal == 3839);
    CHECK(route.points[route.count - 1].ordinal == 0);
    CHECK(ordinalsStrictlyDescending(route));
    CHECK(result.attempts >= 1);
    if (result.truncated != 0) CHECK(result.attempts == kSimplifyAttempts);

    RenderRoute walk{};
    const SimplifyResult walkResult =
        buildFromRecording(straightWalk(3840, 0.07f), walk);
    CHECK(walkResult.truncated == 0);
    CHECK(walk.count <= kMaxRenderPoints);

}

}

namespace self_recall_tests::test_recall_camera_frame {
using namespace self_recall::pure;

TEST_CASE("Camera follows fractional and integer Recall heights without changing the orbit") {
    for (const float rate : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 4.0f}) {
        for (const float verticalSpeed : {-180.0f, 0.0f, 180.0f}) {
            for (const float eyeOffset : {-8.0f, 0.0f, 8.0f}) {
                std::array<float, 10> view{20, 101 + eyeOffset, 30, 20, 101, 22, 0, 1, 0, 0.8f};
                const auto before = view;
                CameraVerticalCorrection correction;
                const float shown = 100 + verticalSpeed * rate / 30;
                REQUIRE(alignCameraHeight(view.data(), shown, 100, 101, correction));
                CHECK(view[4] == doctest::Approx(shown + 1));
                CHECK(view[1] - view[4] == doctest::Approx(eyeOffset));
                for (unsigned i = 0; i < view.size(); ++i)
                    if (i != 1 && i != 4) CHECK(view[i] == before[i]);
            }
        }
    }
}

TEST_CASE("Camera removes a vertical transition offset independently of a cached root delay") {
    std::array<float, 10> view{10, 93, 30, 10, 91, 22, 0, 1, 0, 0.8f};
    CameraVerticalCorrection correction;
    REQUIRE(alignCameraHeight(view.data(), 105, 100, 101, correction));
    CHECK(correction.cachedRootDelta == 5);
    CHECK(correction.transitionDelta == 10);
    CHECK(correction.appliedDelta == 15);
    CHECK(view[1] == 108);
    CHECK(view[4] == 106);
    const auto once = view;
    REQUIRE(alignCameraHeight(view.data(), 105, 100, 101, correction));
    CHECK(view == once);
    CHECK(correction.appliedDelta == 0);
}

TEST_CASE("Camera rejects invalid or overflowing coordinates without partially changing the packet") {
    const std::array<float, 10> original{10, 103, 30, 10, 101, 22, 0, 1, 0, 0.8f};
    CameraVerticalCorrection correction;
    CHECK_FALSE(alignCameraHeight(nullptr, 100, 100, 101, correction));
    for (const float invalid : {std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN()}) {
        auto view = original;
        CHECK_FALSE(alignCameraHeight(view.data(), invalid, 100, 101, correction));
        CHECK(view == original);
        CHECK_FALSE(alignCameraHeight(view.data(), 100, invalid, 101, correction));
        CHECK(view == original);
        CHECK_FALSE(alignCameraHeight(view.data(), 100, 100, invalid, correction));
        CHECK(view == original);
    }
    auto view = original;
    CHECK_FALSE(alignCameraHeight(view.data(), std::numeric_limits<float>::max(),
                                 -std::numeric_limits<float>::max(), 101, correction));
    CHECK(view == original);
    view[9] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(alignCameraHeight(view.data(), 105, 100, 101, correction));
    CHECK(view[1] == original[1]);
    CHECK(view[4] == original[4]);
}
}

namespace self_recall_tests::test_recall_outfit_speed {
using namespace self_recall::pure;

TEST_CASE("Glide speed counts any combination of worn slots") {
    OutfitSpeed speed;
    const std::array expected{PlaybackRate::X125, PlaybackRate::X150,
        PlaybackRate::X150, PlaybackRate::X200, PlaybackRate::X150,
        PlaybackRate::X200, PlaybackRate::X200, PlaybackRate::X400};
    for (std::uint8_t mask = 0; mask < 8; ++mask) {
        REQUIRE(speed.update({mask, {101, 202, 303}}));
        CHECK(speed.rate() == expected[mask]);
        CHECK_FALSE(speed.update({mask, {101, 202, 303}}));
    }
}

TEST_CASE("equipping removing and replacing a counted piece reports the resulting speed once") {
    OutfitSpeed speed;
    CHECK(speed.rate() == PlaybackRate::X125);
    REQUIRE(speed.update({0, {101, 202, 303}}));
    CHECK_FALSE(speed.update({0, {401, 502, 603}}));
    REQUIRE(speed.update({1, {701, 502, 603}}));
    CHECK(speed.rate() == PlaybackRate::X150);
    CHECK_FALSE(speed.update({1, {701, 802, 903}}));
    REQUIRE(speed.update({1, {1001, 802, 903}}));
    CHECK(speed.rate() == PlaybackRate::X150);
    REQUIRE(speed.update({7, {1001, 1002, 1003}}));
    CHECK(speed.rate() == PlaybackRate::X400);
    REQUIRE(speed.update({6, {2001, 1002, 1003}}));
    CHECK(speed.rate() == PlaybackRate::X200);
    REQUIRE(speed.update({4, {2001, 2002, 1003}}));
    CHECK(speed.rate() == PlaybackRate::X150);
    REQUIRE(speed.update({0, {2001, 2002, 2003}}));
    CHECK(speed.rate() == PlaybackRate::X125);
    REQUIRE(speed.update({7, {1, 2, 3}}));
    for (unsigned frame = 0; frame < 120; ++frame) {
        CHECK_FALSE(speed.update({7, {1, 2, 3}}));
        CHECK(speed.rate() == PlaybackRate::X400);
    }
    speed.reset();
    CHECK(speed.rate() == PlaybackRate::X125);
    REQUIRE(speed.update({7, {1, 2, 3}}));
    CHECK(speed.rate() == PlaybackRate::X400);
    CHECK_FALSE(speed.update({7, {1, 2, 3}}));
}

TEST_CASE("a replacement set or individual clothing slots use the same count policy") {
    const OutfitSpeedProfile replacement{"Other outfit", {"TestHead", nullptr, "TestLegs"},
        {PlaybackRate::Normal, PlaybackRate::X175, PlaybackRate::X200, PlaybackRate::X400}};
    OutfitSpeed speed(replacement);
    REQUIRE(speed.update({7, {1, 2, 3}}));
    CHECK(speed.mask() == 5);
    CHECK(speed.count() == 2);
    CHECK(speed.rate() == PlaybackRate::X200);
    REQUIRE(speed.update({2, {1, 2, 3}}));
    CHECK(speed.rate() == PlaybackRate::Normal);
    REQUIRE(speed.update({4, {1, 2, 3}}));
    CHECK(speed.rate() == PlaybackRate::X175);
}
}

namespace self_recall_tests::test_recall_vehicle {
using namespace self_recall::pure;

TEST_CASE("steering ownership ends for the same actor and cannot leak across replacement") {
    NativeVehicleState state;
    CHECK_FALSE(state.active(0));
    CHECK_FALSE(state.active(41));
    state.enter(41);
    CHECK(state.active(41));
    CHECK_FALSE(state.active(42));
    state.enter(42);
    state.leave(41);
    CHECK(state.active(42));
    state.leave(42);
    CHECK_FALSE(state.active(42));
    state.enter(42);
    state.clear();
    CHECK_FALSE(state.active(42));
}

TEST_CASE("steering entry and exit between input and actor phases leave no unsafe history hole") {
    std::array<HistorySample, 5> history{};
    for (unsigned i = 0; i < history.size(); ++i) {
        history[i].pose.rotation.values[0] = 1;
        history[i].pose.rotation.values[4] = 1;
        history[i].pose.rotation.values[8] = 1;
        history[i].pose.position.x = static_cast<float>(i);
    }
    history[0].flags = SampleAdmissible;
    history[1].flags = pairVehicleAdmission(SampleAdmissible | SampleControlStick, true, false);
    history[2].flags = pairVehicleAdmission(SampleAdmissible, true, true);
    history[3].flags = pairVehicleAdmission(0, true, true);
    history[4].flags = SampleAdmissible;
    for (const auto& sample : history) CHECK((sample.flags & SampleAdmissible) != 0);
    const auto route = planRouteProbe(history, false);
    CHECK(route.kind == RouteProbeKind::Vehicle);
    CHECK(route.through == 4);
    CHECK(pairVehicleAdmission(0, false, true) == 0);
    CHECK(pairVehicleAdmission(0, true, false) == 0);
    history[3].flags = 0;
    CHECK(planRouteProbe(std::span(history).subspan(2), false).kind == RouteProbeKind::Unavailable);
}
}

namespace self_recall_tests::test_recall_glider_release {
using namespace self_recall::pure;

namespace {
struct Handoff {
    NativeTraversalState traversal;
    GliderRelease release;
    GliderReleaseContext player{0x1000, 7, 1, 100, true};
    std::uint64_t ticket() { return release.forceTicket(player.actor, player.actorId, traversal); }
    std::uint64_t arm(bool recorded = true) { return release.arm(player, recorded, traversal); }
};
}

TEST_CASE("native traversal observations match IDs including zero and cannot be cleared by foreign leaves") {
    NativeTraversalState native;
    CHECK_FALSE(native.falling(0));
    CHECK_FALSE(native.gliding(UINT32_MAX));
    CHECK_FALSE(native.climbing(0));
    native.enterFall(0);
    native.enterGlide(UINT32_MAX);
    native.enterClimb(0);
    native.leaveFall(8);
    native.leaveGlide(8);
    native.leaveClimb(8);
    CHECK(native.falling(0));
    CHECK(native.gliding(UINT32_MAX));
    CHECK(native.climbing(0));
    CHECK_FALSE(native.climbing(8));
    native.leaveFall(0);
    native.leaveGlide(UINT32_MAX);
    native.leaveClimb(0);
    CHECK_FALSE(native.falling(0));
    CHECK_FALSE(native.gliding(UINT32_MAX));
    CHECK_FALSE(native.climbing(0));
    native.enterFall(7);
    native.enterGlide(7);
    native.enterClimb(UINT32_MAX);
    native.clear();
    CHECK_FALSE(native.falling(7));
    CHECK_FALSE(native.gliding(7));
    CHECK_FALSE(native.climbing(UINT32_MAX));
}

TEST_CASE("release requires recorded native glide and a current native fall from the same player") {
    Handoff h;
    h.traversal.enterFall(h.player.actorId);
    CHECK(h.arm(false) == 0);
    CHECK(h.ticket() == 0);
    const auto serial = h.arm();
    REQUIRE(serial != 0);
    CHECK(h.ticket() == serial);
    CHECK(h.release.forceTicket(0x2000, h.player.actorId, h.traversal) == 0);
    CHECK(h.release.forceTicket(h.player.actor, 8, h.traversal) == 0);
    h.traversal.leaveFall(h.player.actorId);
    CHECK(h.ticket() == 0);
    h.traversal.enterFall(h.player.actorId);
    CHECK(h.ticket() == serial);
    h.traversal.enterGlide(h.player.actorId);
    CHECK(h.ticket() == 0);
    h.release.entered(h.player.actor, h.player.actorId);
    CHECK(h.ticket() == 0);
    CHECK(h.release.result().reason == GliderReleaseEnd::Entered);
    h.traversal.leaveGlide(h.player.actorId);
    CHECK(h.ticket() == 0);
}

TEST_CASE("glider release is bounded to 45 input ticks and reports its admitted overrides") {
    Handoff h;
    h.traversal.enterFall(h.player.actorId);
    const auto serial = h.arm();
    REQUIRE(h.release.confirmForce(h.ticket()));
    h.player.tick += 44;
    h.release.service(h.player, false);
    REQUIRE(h.release.confirmForce(h.ticket()));
    h.player.tick += 1;
    h.release.service(h.player, false);
    CHECK(h.ticket() == 0);
    CHECK_FALSE(h.release.confirmForce(serial));
    CHECK(h.release.result().serial == serial);
    CHECK(h.release.result().reason == GliderReleaseEnd::TimedOut);
    CHECK(h.release.result().forcedCalls == 2);
}

TEST_CASE("invalid release context and already gliding do not arm a forced transition") {
    Handoff h;
    SUBCASE("missing actor") { h.player.actor = 0; }
    SUBCASE("missing generation") { h.player.worldGeneration = 0; }
    SUBCASE("unsafe") { h.player.allowed = false; }
    SUBCASE("tick addition would wrap") { h.player.tick = UINT64_MAX - GliderRelease::kAcquireTicks + 1; }
    SUBCASE("native glider is already open") { h.traversal.enterGlide(h.player.actorId); }
    CHECK(h.arm() == 0);
}

TEST_CASE("changed player generation safety and backwards clock invalidate a pending glider release") {
    Handoff h;
    REQUIRE(h.arm() != 0);
    SUBCASE("recycled address") { ++h.player.actorId; }
    SUBCASE("different address") { ++h.player.actor; }
    SUBCASE("world generation") { ++h.player.worldGeneration; }
    SUBCASE("unsafe state") { h.player.allowed = false; }
    SUBCASE("clock reset") { --h.player.tick; }
    h.release.service(h.player, false);
    CHECK(h.ticket() == 0);
    CHECK(h.release.result().reason == GliderReleaseEnd::ContextChanged);
}

TEST_CASE("late selector result cannot authorize or cancel a replacement request") {
    Handoff h;
    h.traversal.enterFall(h.player.actorId);
    const auto old = h.arm();
    REQUIRE(old != 0);
    REQUIRE(h.ticket() == old);
    h.release.cancel(GliderReleaseEnd::NewRecall);
    const auto current = h.arm();
    REQUIRE(current > old);
    CHECK_FALSE(h.release.confirmForce(old));
    h.release.cancelTicket(old, GliderReleaseEnd::Unavailable);
    CHECK(h.ticket() == current);
    CHECK(h.release.confirmForce(current));
    h.release.entered(0x2000, h.player.actorId);
    h.release.entered(h.player.actor, 8);
    CHECK(h.ticket() == current);
    h.release.entered(h.player.actor, h.player.actorId);
    CHECK(h.release.result().serial == current);
    CHECK(h.release.result().reason == GliderReleaseEnd::Entered);
    CHECK(h.release.result().forcedCalls == 1);
    h.release.cancelTicket(old, GliderReleaseEnd::TimedOut);
    CHECK(h.release.result().serial == current);
}

TEST_CASE("a later user cancel and lost paraglider ownership end acquisition permanently") {
    Handoff h;
    h.traversal.enterFall(h.player.actorId);
    const auto serial = h.arm();
    REQUIRE(serial != 0);
    SUBCASE("user cancel") {
        h.release.service(h.player, true);
        CHECK(h.release.result().reason == GliderReleaseEnd::Cancelled);
    }
    SUBCASE("native acquisition gate failed") {
        h.release.cancelTicket(h.ticket(), GliderReleaseEnd::Unavailable);
        CHECK(h.release.result().reason == GliderReleaseEnd::Unavailable);
    }
    CHECK(h.ticket() == 0);
    CHECK_FALSE(h.release.confirmForce(serial));
}
}

namespace self_recall_tests::test_recall_stop_policy {
using namespace self_recall;

TEST_CASE("typed playback exits preserve messages and glider-release decisions") {
    struct Case { pure::PlaybackStop stop; const char* reason; const char* event; };
    const std::array cases{
        Case{pure::PlaybackStop::PoseApplyFailed, "POSE_APPLY_FAILED", "stopped: player pose unavailable"},
        Case{pure::PlaybackStop::ClockUnavailable, "CLOCK_UNAVAILABLE", "stopped: gameplay clock unavailable"},
        Case{pure::PlaybackStop::RayBlocked, "RAY_BLOCKED", "path blocked - stopped in place"},
        Case{pure::PlaybackStop::RouteUnavailable, "ROUTE_UNAVAILABLE", "stopped: route check unavailable"},
        Case{pure::PlaybackStop::NonfiniteCurrent, "NONFINITE_CURRENT", "stopped: pose became invalid"},
        Case{pure::PlaybackStop::Blocked, "BLOCKED", "path blocked - stopped in place"},
        Case{pure::PlaybackStop::Finished, "FINISHED", "route fully rewound"},
        Case{pure::PlaybackStop::UnsafeSample, "UNSAFE_SAMPLE", "unsafe move reached - stopped in place"},
        Case{pure::PlaybackStop::PoseUnavailable, "POSE_UNAVAILABLE", "stopped: recorded animation unavailable"},
        Case{pure::PlaybackStop::NonfiniteSample, "NONFINITE_SAMPLE", "bad sample - stopped in place"},
        Case{pure::PlaybackStop::NativePathFailed, "NATIVE_PATH_FAILED", "stopped: Recall path unavailable"},
        Case{pure::PlaybackStop::PaletteFailed, "PALETTE_FAILED", "stopped: Recall colors unavailable"},
        Case{pure::PlaybackStop::AnimationRenderFailed, "ANIMATION_RENDER_FAILED", "stopped: recorded animation unavailable"},
        Case{pure::PlaybackStop::UnsafeLiveState, "UNSAFE_LIVE_STATE", "another move took over - stopped in place"},
        Case{pure::PlaybackStop::CancelledByB, "CANCELLED_BY_B", "cancelled"},
        Case{pure::PlaybackStop::StaminaExhausted, "STAMINA_EXHAUSTED", "Recall ended: stamina depleted"},
        Case{pure::PlaybackStop::StaminaUnavailable, "STAMINA_UNAVAILABLE", "stopped: stamina unavailable"},
    };
    CHECK(cases.size() == static_cast<unsigned>(pure::PlaybackStop::Count));
    for (const auto& item : cases) {
        const auto text = playbackStopText(item.stop);
        CHECK(std::string_view(text.reason) == item.reason);
        CHECK(std::string_view(text.event) == item.event);
        const bool releasable = item.stop == pure::PlaybackStop::Finished ||
                                item.stop == pure::PlaybackStop::CancelledByB;
        for (unsigned safe = 0; safe <= 1; ++safe) {
            for (unsigned flags = 0; flags < 8; ++flags) {
                const auto exit = pure::playbackExitPlan(item.stop, safe != 0, static_cast<std::uint8_t>(flags));
                const bool expectedRelease = releasable && safe &&
                    (flags & pure::SampleNativeGlide) && (flags & pure::SampleAdmissible);
                CHECK(exit.releaseGlider == expectedRelease);
            }
        }
    }
}
}

namespace self_recall_tests::test_recall_stamina {
using namespace self_recall::pure;

TEST_CASE("Recall can spend a partial wheel or bonus stamina and refuses exhausted or invalid values") {
    CHECK(staminaStatus(0.01f, 0) == StaminaStatus::Available);
    CHECK(staminaStatus(0, 0.01f) == StaminaStatus::Available);
    CHECK(staminaStatus(0, 0) == StaminaStatus::Empty);
    CHECK(staminaStatus(-1, 100) == StaminaStatus::Unavailable);
    CHECK(staminaStatus(100, std::numeric_limits<float>::infinity()) == StaminaStatus::Unavailable);
    CHECK(staminaStatus(std::numeric_limits<float>::quiet_NaN(), 0) == StaminaStatus::Unavailable);
}

TEST_CASE("release damage protection expires and cannot cross a clock reset") {
    constexpr std::uint64_t released = 9000000000;
    constexpr auto until = released + kRecallReleaseProtectionNs;
    CHECK(releaseProtectionActive(released, until));
    CHECK(releaseProtectionActive(until - 1, until));
    CHECK_FALSE(releaseProtectionActive(until, until));
    CHECK_FALSE(releaseProtectionActive(until + 1, until));
    CHECK_FALSE(releaseProtectionActive(1, until));
    CHECK_FALSE(releaseProtectionActive(released, 0));
}
}

namespace self_recall_tests::test_recall_motion_continuity {
using namespace self_recall::pure;

TEST_CASE("fast falling and launches preserve motion but unrelated warps reset history") {
    const float zero[3]{}, falling[3]{0, -150, 0}, fling[3]{2000, 0, 0};
    CHECK_FALSE(motionDiscontinuity({}, {0, -50, 0}, falling, falling, 1.0 / 3, 30));
    CHECK_FALSE(motionDiscontinuity({}, {66, 0, 0}, zero, fling, 1.0 / 30, 30));
    CHECK(motionDiscontinuity({}, {0, 50, 0}, falling, falling, 1.0 / 3, 30));
    CHECK(motionDiscontinuity({}, {0, 0, 50}, zero, fling, 1.0 / 30, 30));
    CHECK(motionDiscontinuity({}, {0, -50, 0}, zero, zero, 1.0 / 3, 30));
    CHECK_FALSE(motionDiscontinuity({}, {30, 0, 0}, nullptr, nullptr, 0, 30));
}
TEST_CASE("paused stale and corrupt time cannot excuse a teleport") {
    const float falling[3]{0, -150, 0};
    const float invalid[3]{0, std::numeric_limits<float>::quiet_NaN(), 0};
    for (double seconds : {0.0, -1.0, 1.1, std::numeric_limits<double>::quiet_NaN()})
        CHECK(motionDiscontinuity({}, {0, -50, 0}, falling, falling, seconds, 30));
    CHECK(motionDiscontinuity({}, {0, -50, 0}, nullptr, invalid, 0.5, 30));
}
}
