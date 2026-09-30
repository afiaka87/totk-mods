// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>

#include <cmath>
#include <initializer_list>
#include <limits>

#include "ChainVisual.hpp"

using namespace zonai_hookshot::pure;

namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

TEST_CASE("the chain is at the anchor on the first step, at any range") {
    for (const float range : {0.5f, 30.0f, 350.0f, 1000.0f}) {
        LaunchState s{};
        s.fireOrigin = {0.0f, 0.0f, 0.0f};
        s.anchor = {range, 0.0f, 0.0f};
        CHECK(stepLaunch(s));
        CHECK(s.ticks == 1);
        CHECK(launchTip(s).x == doctest::Approx(range));
    }
}

TEST_CASE("a launch step survives a non-finite anchor without spreading NaN") {
    LaunchState s{};
    s.fireOrigin = {0.0f, 0.0f, 0.0f};
    s.anchor = {kNaN, 0.0f, 0.0f};
    CHECK(stepLaunch(s));
    CHECK(std::isfinite(s.advanced));
    CHECK_FALSE(launchSnapshot({0.0f, 0.0f, 0.0f}, s).visible);
}

TEST_CASE("latched near endpoint follows the live origin") {
    const Vec3 anchor = {10.0f, 10.0f, 10.0f};
    auto a = latched({0.0f, 0.0f, 0.0f}, anchor);
    auto b = latched({5.0f, 0.0f, 0.0f}, anchor);
    CHECK(a.visible);
    CHECK(b.visible);
    CHECK(a.nearPoint.x == 0.0f);
    CHECK(b.nearPoint.x == 5.0f);
    CHECK(a.farPoint.x == anchor.x);
    CHECK(b.farPoint.x == anchor.x);
    CHECK(a.style == ChainStyle::Latched);
}

TEST_CASE("preview styles follow the verdict") {
    TargetSample s{};
    s.completed = true;
    s.hit = true;
    s.position = {10.0f, 0.0f, 0.0f};

    SUBCASE("valid target draws the valid style") {
        auto snap = preview({0.0f, 0.0f, 0.0f}, s, Verdict::Valid);
        CHECK(snap.visible);
        CHECK(snap.style == ChainStyle::PreviewValid);
    }
    SUBCASE("refused-but-real hit draws the invalid style") {
        auto snap = preview({0.0f, 0.0f, 0.0f}, s, Verdict::NoClimb);
        CHECK(snap.visible);
        CHECK(snap.style == ChainStyle::PreviewInvalid);
    }
    SUBCASE("dynamic-body refusal draws the invalid style") {
        auto snap = preview({0.0f, 0.0f, 0.0f}, s, Verdict::MovingOrUnsupported);
        CHECK(snap.visible);
        CHECK(snap.style == ChainStyle::PreviewInvalid);
    }
    SUBCASE("pending and miss draw nothing") {
        CHECK_FALSE(preview({0.0f, 0.0f, 0.0f}, s, Verdict::Pending).visible);
        CHECK_FALSE(preview({0.0f, 0.0f, 0.0f}, s, Verdict::Miss).visible);
        CHECK_FALSE(preview({0.0f, 0.0f, 0.0f}, s, Verdict::InvalidFinite).visible);
    }
}

TEST_CASE("targeting is diamond-only and the chain body begins on fire") {
    CHECK_FALSE(chainBodyVisible(ChainStyle::None));
    CHECK_FALSE(chainBodyVisible(ChainStyle::PreviewValid));
    CHECK_FALSE(chainBodyVisible(ChainStyle::PreviewInvalid));
    CHECK(chainBodyVisible(ChainStyle::Launch));
    CHECK(chainBodyVisible(ChainStyle::Latched));
}

TEST_CASE("implausible snapshots are rejected") {
    SUBCASE("non-finite origin") {
        auto snap = latched({kNaN, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f});
        CHECK_FALSE(snap.visible);
    }
    SUBCASE("non-finite anchor") {
        auto snap = latched({0.0f, 0.0f, 0.0f}, {kNaN, 0.0f, 0.0f});
        CHECK_FALSE(snap.visible);
    }
    SUBCASE("collapsed span") {
        auto snap = latched({1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.001f});
        CHECK_FALSE(snap.visible);
    }
    SUBCASE("absurd span") {
        auto snap = latched({0.0f, 0.0f, 0.0f}, {500.0f, 0.0f, 0.0f});
        CHECK_FALSE(snap.visible);
    }
    SUBCASE("launch snapshot with NaN anchor") {
        LaunchState s{};
        s.fireOrigin = {0.0f, 0.0f, 0.0f};
        s.anchor = {kNaN, 0.0f, 0.0f};
        auto snap = launchSnapshot({0.0f, 0.0f, 0.0f}, s);
        CHECK_FALSE(snap.visible);
    }
}
