// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <doctest.h>
#include "../src/wolf/FollowTarget.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <limits>

using namespace botw_abilities::wolf;
using totk::core::WorldPosition;
using totk::core::WorldTransform;

namespace {
constexpr WorldTransform frame{{{1,0,0, 0,1,0, 0,0,1}}, {100,20,200}};
void samePoint(WorldPosition actual, WorldPosition expected) {
    CHECK(std::bit_cast<std::uint32_t>(actual.x) == std::bit_cast<std::uint32_t>(expected.x));
    CHECK(std::bit_cast<std::uint32_t>(actual.y) == std::bit_cast<std::uint32_t>(expected.y));
    CHECK(std::bit_cast<std::uint32_t>(actual.z) == std::bit_cast<std::uint32_t>(expected.z));
}
}

TEST_CASE("Wolf follow targets ride closer as the player speeds up") {
    const FollowDistances distances;
    CHECK(followOffset(distances, 0, .8f, 5, FollowSide::Positive).forward == 3.5f);
    CHECK(followOffset(distances, .185f, .8f, 5, FollowSide::Positive).forward == 3.0f);
    CHECK(followOffset(distances, .27f, .8f, 5, FollowSide::Positive).forward == 3.0f);
    CHECK(followOffset(distances, .355f, .8f, 5, FollowSide::Positive).forward == 3.0f);
    samePoint(followWorldPoint(frame, followOffset(distances, 0, .8f, 0, FollowSide::Positive)),
              {105.0199966430664f,20,203.5f});
    samePoint(followWorldPoint(frame, followOffset(distances, .27f, .8f, 5, FollowSide::Negative)),
              {97.48999786376953f,20,203});
}

TEST_CASE("Wolf follow converts native velocity to the original per-frame horizontal speed") {
    CHECK(originalFollowSpeed({0,100,0}) == 0);
    CHECK(originalFollowSpeed({5.55f,0,0}) == doctest::Approx(.185f));
    CHECK(originalFollowSpeed({0,0,-8.1f}) == doctest::Approx(.27f));
    CHECK(originalFollowSpeed({3,-90,4}) == doctest::Approx(1.0f/6.0f));
    CHECK(originalFollowSpeed({-3,90,-4}) == originalFollowSpeed({3,0,4}));
    CHECK(followOffset({}, originalFollowSpeed({30,0,0}), .8f, 5, FollowSide::Positive).forward == 3.0f);
}

TEST_CASE("Wolf follow search matches the nine original ARM candidate queries") {
    constexpr std::array<WorldPosition, 9> original{{
        {102.51000213623047f,20,203.5f}, {102.51000213623047f,20,203.5f},
        {100.836669921875f,20,201.1666717529297f}, {99.163330078125f,20,198.8333282470703f},
        {102.51000213623047f,20,203.5f}, {100.836669921875f,20,201.1666717529297f},
        {99.163330078125f,20,198.8333282470703f}, {100,20,203.5f}, {100,20,205.8333282470703f}}};
    const auto offset = followOffset({}, 0, .8f, 5, FollowSide::Positive);
    for (std::size_t accepted = 0; accepted <= original.size(); ++accepted) {
        std::size_t calls = 0;
        const auto target = selectFollowTarget(frame, offset, [&](WorldPosition point) -> std::optional<WorldPosition> {
            REQUIRE(calls < original.size());
            samePoint(point, original[calls]);
            return calls++ == accepted ? std::optional{point} : std::nullopt;
        });
        CHECK(calls == (accepted < original.size() ? accepted + 1 : original.size()));
        samePoint(target, accepted < original.size() ? original[accepted] : frame.position);
    }
    CHECK_FALSE(followCandidate(offset, kFollowCandidateCount));
    CHECK_FALSE(followCandidate(offset, std::numeric_limits<std::size_t>::max()));
}

TEST_CASE("Wolf follow search retains rotation and nav-adjusted output") {
    constexpr WorldTransform rotated{{{0,0,1, 0,1,0, -1,0,0}}, {100,20,200}};
    const auto offset = followOffset({}, 0, .8f, 5, FollowSide::Positive);
    samePoint(followWorldPoint(rotated, offset), {103.5f,20,197.49000549316406f});
    samePoint(selectFollowTarget(rotated, offset, [](WorldPosition point) {
        point.y = 21.25f;
        return std::optional{point};
    }), {103.5f,21.25f,197.49000549316406f});
}

TEST_CASE("Wolf follow side and reference frame preserve strict threshold boundaries") {
    for (auto previous : {FollowSide::Unset, FollowSide::Positive, FollowSide::Negative}) {
        CHECK(followSide(previous, .5f, .8f) == previous);
        CHECK(followSide(previous, .499f, .8f) == FollowSide::Negative);
        CHECK(followSide(previous, .501f, .8f) == FollowSide::Positive);
    }
    constexpr float angle = 0.06981316953897476f;
    CHECK_FALSE(captureFollowFrame(.999f, 0));
    CHECK(captureFollowFrame(1, 0));
    CHECK_FALSE(captureFollowFrame(1, angle));
    CHECK(captureFollowFrame(1, std::nextafter(angle, 0.0f)));
    CHECK_FALSE(captureFollowFrame(20, angle + .01f));
}

TEST_CASE("Wolf follow retries above only when the lower surface probe fails") {
    for (const bool lower : {false, true}) {
        for (const bool upper : {false, true}) {
            for (const bool reachable : {false, true}) {
                int surfaceCalls = 0, pathCalls = 0;
                const auto result = probeFollowTarget(frame.position,
                    [&](WorldPosition start, WorldPosition end) -> std::optional<WorldPosition> {
                        samePoint(start, frame.position);
                        const bool down = surfaceCalls++ == 0;
                        samePoint(end, {100, down ? 15.0f : 25.0f, 200});
                        return (down ? lower : upper) ? std::optional{end} : std::nullopt;
                    },
                    [&](WorldPosition hit) -> std::optional<WorldPosition> {
                        ++pathCalls;
                        samePoint(hit, {100, lower ? 15.0f : 25.0f, 200});
                        return reachable ? std::optional{WorldPosition{101, hit.y, 202}} : std::nullopt;
                    });
                CHECK(surfaceCalls == (lower ? 1 : 2));
                CHECK(pathCalls == (lower || upper ? 1 : 0));
                CHECK(result.has_value() == ((lower || upper) && reachable));
                if (result) samePoint(*result, {101, lower ? 15.0f : 25.0f, 202});
            }
        }
    }
}

TEST_CASE("Wolf follow keeps the captured frame after every surface is rejected") {
    int surfaces = 0, paths = 0;
    const auto offset = followOffset({}, 0, .8f, 5, FollowSide::Positive);
    const auto result = selectFollowTarget(frame, offset, [&](WorldPosition candidate) {
        return probeFollowTarget(candidate,
            [&](WorldPosition, WorldPosition) -> std::optional<WorldPosition> {
                ++surfaces;
                return std::nullopt;
            },
            [&](WorldPosition point) -> std::optional<WorldPosition> {
                ++paths;
                return point;
            });
    });
    samePoint(result, frame.position);
    CHECK(surfaces == 18);
    CHECK(paths == 0);
}
