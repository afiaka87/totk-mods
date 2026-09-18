// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>

#include <cmath>
#include <limits>

#include "TargetValidator.hpp"

using namespace zonai_hookshot::pure;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

TargetSample validSample() {
    TargetSample s{};
    s.completed = true;
    s.hit = true;
    s.position = {10.0f, 5.0f, 0.0f};
    s.normal = {-1.0f, 0.0f, 0.0f};      // wall facing -X
    s.range = 12.0f;
    s.shapeFlags = 0x8000;               // NoStick material bit set, NoClimb clear
    s.rayDirection = {1.0f, 0.0f, 0.0f}; // aiming +X into the wall
    s.generation = 7;
    s.sequence = 42;
    s.hitBodyKnown = true;
    s.hitMotionType = kMotionStatic;
    return s;
}

SampleContext freshContext() {
    SampleContext ctx{};
    ctx.currentGeneration = 7;
    ctx.latestResultSequence = 42;
    ctx.sampleAgeTicks = 1;
    return ctx;
}

}  // namespace

TEST_CASE("validator accepts the reference wall sample") {
    CHECK(validate(validSample(), freshContext()) == Verdict::Valid);
}

TEST_CASE("validator pending states") {
    auto ctx = freshContext();
    auto s = validSample();

    SUBCASE("not completed") {
        s.completed = false;
        CHECK(validate(s, ctx) == Verdict::Pending);
    }
    SUBCASE("world generation mismatch") {
        s.generation = 6;
        CHECK(validate(s, ctx) == Verdict::Pending);
    }
    SUBCASE("sequence mismatch - old result under a new request counter") {
        ctx.latestResultSequence = 43;
        CHECK(validate(s, ctx) == Verdict::Pending);
    }
    SUBCASE("stale sample beyond the preview freshness budget") {
        ctx.sampleAgeTicks = ValidatorConfig{}.previewMaxAgeTicks + 1;
        CHECK(validate(s, ctx) == Verdict::Pending);
    }
    SUBCASE("age exactly at the budget still shows") {
        ctx.sampleAgeTicks = ValidatorConfig{}.previewMaxAgeTicks;
        CHECK(validate(s, ctx) == Verdict::Valid);
    }
}

TEST_CASE("validator miss") {
    auto s = validSample();
    s.hit = false;
    CHECK(validate(s, freshContext()) == Verdict::Miss);
}

TEST_CASE("validator non-finite rejection") {
    auto ctx = freshContext();

    SUBCASE("NaN position") {
        auto s = validSample();
        s.position.y = kNaN;
        CHECK(validate(s, ctx) == Verdict::InvalidFinite);
    }
    SUBCASE("infinite normal") {
        auto s = validSample();
        s.normal.x = kInf;
        CHECK(validate(s, ctx) == Verdict::InvalidFinite);
    }
    SUBCASE("NaN range") {
        auto s = validSample();
        s.range = kNaN;
        CHECK(validate(s, ctx) == Verdict::InvalidFinite);
    }
    SUBCASE("NaN ray direction must NOT fail open") {
        auto s = validSample();
        s.rayDirection = {kNaN, kNaN, kNaN};
        CHECK(validate(s, ctx) == Verdict::InvalidFinite);
    }
    SUBCASE("zero ray direction is degenerate") {
        auto s = validSample();
        s.rayDirection = {0.0f, 0.0f, 0.0f};
        CHECK(validate(s, ctx) == Verdict::InvalidFinite);
    }
    SUBCASE("degenerate short normal") {
        auto s = validSample();
        s.normal = {0.1f, 0.0f, 0.0f};
        CHECK(validate(s, ctx) == Verdict::InvalidFinite);
    }
    SUBCASE("oversized normal") {
        auto s = validSample();
        s.normal = {2.0f, 0.0f, 0.0f};
        CHECK(validate(s, ctx) == Verdict::InvalidFinite);
    }
}

TEST_CASE("validator hit-body policy (2026-07-20 v0.1.2 re-test: static-only again)") {
    auto ctx = freshContext();

    SUBCASE("unresolved hit body fails closed") {
        auto s = validSample();
        s.hitBodyKnown = false;
        CHECK(validate(s, ctx) == Verdict::MovingOrUnsupported);
    }
    SUBCASE("keyframed body refused") {
        auto s = validSample();
        s.hitMotionType = 1;
        CHECK(validate(s, ctx) == Verdict::MovingOrUnsupported);
    }
    SUBCASE("dynamic body refused") {
        auto s = validSample();
        s.hitMotionType = 2;
        CHECK(validate(s, ctx) == Verdict::MovingOrUnsupported);
    }
    SUBCASE("unknown motion class refused") {
        auto s = validSample();
        s.hitMotionType = 7;
        CHECK(validate(s, ctx) == Verdict::MovingOrUnsupported);
    }
    SUBCASE("movable refusal outranks the NoClimb material read") {
        auto s = validSample();
        s.hitMotionType = 2;
        s.shapeFlags = 0x8001;  // NoClimb bit set on a dynamic body
        CHECK(validate(s, ctx) == Verdict::MovingOrUnsupported);
    }
}

TEST_CASE("validator surface policy") {
    auto ctx = freshContext();

    SUBCASE("NoClimb bit refuses") {
        auto s = validSample();
        s.shapeFlags = 0x8001;
        CHECK(validate(s, ctx) == Verdict::NoClimb);
    }
    SUBCASE("too near") {
        auto s = validSample();
        s.range = 1.0f;
        CHECK(validate(s, ctx) == Verdict::TooNear);
    }
    SUBCASE("too far") {
        auto s = validSample();
        s.range = 301.0f;
        CHECK(validate(s, ctx) == Verdict::TooFar);
    }
    SUBCASE("cross-gap distance accepted (2026-07-20 range extension)") {
        auto s = validSample();
        s.range = 250.0f;
        CHECK(validate(s, ctx) == Verdict::Valid);
    }
    SUBCASE("floor normal refused") {
        auto s = validSample();
        s.normal = {0.0f, 1.0f, 0.0f};
        s.rayDirection = {0.6f, -0.8f, 0.0f};  // looking down at it
        CHECK(validate(s, ctx) == Verdict::Floor);
    }
    SUBCASE("ceiling normal refused") {
        auto s = validSample();
        s.normal = {0.0f, -1.0f, 0.0f};
        s.rayDirection = {0.6f, 0.8f, 0.0f};   // looking up at it
        CHECK(validate(s, ctx) == Verdict::CeilingOrOverhang);
    }
    SUBCASE("shallow underhang inside the wall band accepted") {
        auto s = validSample();
        s.normal = {-0.94f, -0.34f, 0.0f};     // leaning face, normal.y ~ -0.34
        CHECK(validate(s, ctx) == Verdict::Valid);
    }
    SUBCASE("backface refused") {
        auto s = validSample();
        s.normal = {1.0f, 0.0f, 0.0f};         // facing away from the ray
        CHECK(validate(s, ctx) == Verdict::Backface);
    }
}

TEST_CASE("confirm decision commit path") {
    auto ctx = freshContext();
    auto s = validSample();

    SUBCASE("fresh post-press sample commits") {
        CHECK(confirmDecision(s, ctx, 42) == ConfirmDecision::Commit);
    }
    SUBCASE("sample from before the press keeps waiting") {
        CHECK(confirmDecision(s, ctx, 43) == ConfirmDecision::Waiting);
    }
    SUBCASE("stale-but-matching sample keeps waiting") {
        ctx.sampleAgeTicks = ValidatorConfig{}.confirmMaxAgeTicks + 1;
        CHECK(confirmDecision(s, ctx, 42) == ConfirmDecision::Waiting);
    }
    SUBCASE("wrong generation keeps waiting") {
        ctx.currentGeneration = 8;
        CHECK(confirmDecision(s, ctx, 42) == ConfirmDecision::Waiting);
    }
    SUBCASE("fresh post-press invalid target refuses") {
        s.shapeFlags = 0x8001;  // NoClimb
        CHECK(confirmDecision(s, ctx, 42) == ConfirmDecision::Refuse);
    }
    SUBCASE("fresh post-press miss refuses") {
        s.hit = false;
        CHECK(confirmDecision(s, ctx, 42) == ConfirmDecision::Refuse);
    }
    SUBCASE("fresh post-press unresolved body refuses") {
        s.hitBodyKnown = false;
        CHECK(confirmDecision(s, ctx, 42) == ConfirmDecision::Refuse);
    }
    SUBCASE("fresh post-press dynamic body refuses (2026-07-20 static-only)") {
        s.hitMotionType = 2;
        CHECK(confirmDecision(s, ctx, 42) == ConfirmDecision::Refuse);
    }
}

TEST_CASE("confirm refusal reason never launders a stale preview as VALID") {
    auto ctx = freshContext();
    auto s = validSample();

    SUBCASE("valid-looking preview from before the press reads Pending") {
        REQUIRE(validate(s, ctx) == Verdict::Valid);
        CHECK(confirmRefusalReason(s, ctx, 43) == Verdict::Pending);
    }
    SUBCASE("stale-but-matching valid preview reads Pending") {
        ctx.sampleAgeTicks = ValidatorConfig{}.confirmMaxAgeTicks + 1;
        CHECK(confirmRefusalReason(s, ctx, 42) == Verdict::Pending);
    }
    SUBCASE("wrong generation reads Pending") {
        ctx.currentGeneration = 8;
        CHECK(confirmRefusalReason(s, ctx, 42) == Verdict::Pending);
    }
    SUBCASE("fresh post-press invalid target reports its real verdict") {
        s.shapeFlags = 0x8001;  // NoClimb
        CHECK(confirmRefusalReason(s, ctx, 42) == Verdict::NoClimb);
        s = validSample();
        s.hitBodyKnown = false;
        CHECK(confirmRefusalReason(s, ctx, 42) == Verdict::MovingOrUnsupported);
    }
    SUBCASE("agrees with confirmDecision on the gate") {
        CHECK(confirmDecision(s, ctx, 43) == ConfirmDecision::Waiting);
        CHECK(confirmRefusalReason(s, ctx, 43) == Verdict::Pending);
        s.hit = false;
        CHECK(confirmDecision(s, ctx, 42) == ConfirmDecision::Refuse);
        CHECK(confirmRefusalReason(s, ctx, 42) == Verdict::Miss);
    }
}
