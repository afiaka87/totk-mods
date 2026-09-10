#include <doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "RecallAnimationPolicy.hpp"
#include "RecallVisual.hpp"

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

}  // namespace

TEST_CASE("simplification preserves the newest and oldest endpoints") {
    std::vector<FedSample> samples;
    for (int i = 0; i < 300; ++i) {
        const FedSample sample{
            {static_cast<float>(i) * 0.15f, 20.0f, 3.0f}, 0};
        samples.push_back(sample);
        samples.push_back(sample);  // unmoved twin (30 Hz world)
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

    bool cornerKept = false;
    for (std::uint16_t i = 0; i < route.count; ++i) {
        if (distance(route.points[i].position, {12.0f, 5.0f, 0.0f}) <
            kDefaultSimplifyParams.straightSpacingMeters)
            cornerKept = true;
    }
    CHECK(cornerKept);

    bool transitionKept = false;
    for (std::uint16_t i = 0; i < route.count; ++i) {
        if (distance(route.points[i].position, {12.0f, 5.1f, 12.0f}) < 0.75f)
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

TEST_CASE("forward animation allowlist refuses arbitrary commands") {
    CHECK(allowlistedCommandName(kKindNone) == nullptr);
    CHECK(allowlistedCommandName(kKindCount) == nullptr);
    CHECK(allowlistedCommandName(200) == nullptr);
    CHECK(allowlistedCommandName(0xff) == nullptr);
    CHECK(std::strcmp(allowlistedCommandName(kKindMove), "Move") == 0);
    CHECK(std::strcmp(allowlistedCommandName(kKindClimbMove),
                      "ClimbMove") == 0);
    CHECK(std::strcmp(allowlistedCommandName(kKindClimbWait),
                      "ClimbWait") == 0);
    CHECK(std::strcmp(allowlistedCommandName(kKindGlide), "Glide") == 0);
    CHECK(std::strcmp(allowlistedCommandName(kKindFall), "Fall") == 0);
    CHECK(std::strcmp(allowlistedCommandName(kKindParasailGlide),
                      "ParasailGlide") == 0);
    CHECK_FALSE(isAllowlistedKind(kKindNone));
    CHECK(isAllowlistedKind(kKindMove));
}

TEST_CASE("forward rate policy can never produce zero or a negative rate") {
    CHECK(forwardAnimationRate(0.0f) == doctest::Approx(0.25f));
    CHECK(forwardAnimationRate(-0.0f) == doctest::Approx(0.25f));
    CHECK(forwardAnimationRate(1.0f) == doctest::Approx(1.0f));
    CHECK(forwardAnimationRate(-1.3f) == doctest::Approx(1.3f));
    CHECK(forwardAnimationRate(5.0f) == doctest::Approx(2.0f));
    CHECK(forwardAnimationRate(-100.0f) == doctest::Approx(2.0f));
    const float nan = std::nanf("");
    CHECK(forwardAnimationRate(nan) == doctest::Approx(1.0f));
    CHECK(forwardAnimationRate(INFINITY) == doctest::Approx(1.0f));
    for (float rate = -4.0f; rate <= 4.0f; rate += 0.03125f) {
        const float sanitized = forwardAnimationRate(rate);
        CHECK(sanitized >= 0.25f);
        CHECK(sanitized <= 2.0f);
    }
}

TEST_CASE("anim lane cycle visits all three lanes and returns home") {
    using self_recall::pure::AnimLane;
    using self_recall::pure::animLaneName;
    using self_recall::pure::nextAnimLane;
    AnimLane lane = AnimLane::ForwardResume;
    lane = nextAnimLane(lane);
    CHECK(lane == AnimLane::Reversed);
    lane = nextAnimLane(lane);
    CHECK(lane == AnimLane::Plain);
    lane = nextAnimLane(lane);
    CHECK(lane == AnimLane::ForwardResume);
    CHECK(std::strcmp(animLaneName(AnimLane::ForwardResume),
                      "anim: forward-resume") == 0);
    CHECK(std::strcmp(animLaneName(AnimLane::Reversed),
                      "anim: reversed") == 0);
    CHECK(std::strcmp(animLaneName(AnimLane::Plain), "anim: plain") == 0);
    CHECK(nextAnimLane(static_cast<AnimLane>(7)) ==
          AnimLane::ForwardResume);
    CHECK(std::strcmp(animLaneName(static_cast<AnimLane>(7)),
                      "anim: plain") == 0);
}

TEST_CASE("frame-force classification: applied, dead, bent") {
    using self_recall::pure::classifyFrameForce;
    using self_recall::pure::FrameForceResult;
    CHECK(classifyFrameForce(3.0f, 42.0f, 42.0f) ==
          FrameForceResult::Applied);
    CHECK(classifyFrameForce(3.0f, 41.9f, 42.0f) ==
          FrameForceResult::Applied);
    CHECK(classifyFrameForce(10.0f, 10.0f, 55.0f) ==
          FrameForceResult::Dead);
    CHECK(classifyFrameForce(10.0f, 10.0f, 10.4f) ==
          FrameForceResult::Applied);
    CHECK(classifyFrameForce(10.0f, 0.0f, 55.0f) == FrameForceResult::Bent);
    CHECK(classifyFrameForce(58.0f, 2.0f, 62.0f) == FrameForceResult::Bent);
    CHECK(classifyFrameForce(58.0f, 2.0f, 2.0f) ==
          FrameForceResult::Applied);
}
