// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <limits>
#include <initializer_list>
#include "TravelBodyAnchor.hpp"
#include "TravelBodyReadShift.hpp"
#include "ArrowFlightPresentation.hpp"
using namespace zonai_hookshot::pure;

TEST_CASE("arrow presentation owns only live glide following and fades physics at high speed") {
    using arrowbound::pure::ArrowPhase;
    for (const auto phase : {ArrowPhase::Idle, ArrowPhase::WaitingForArrow,
                            ArrowPhase::WallProbe, ArrowPhase::WallGrip, ArrowPhase::Bailout})
        CHECK_FALSE(arrowFlightPresentation(phase, true, false, {237, 0, 0}).anchor);
    CHECK_FALSE(arrowFlightPresentation(ArrowPhase::Following, false, false, {237, 0, 0}).anchor);
    CHECK_FALSE(arrowFlightPresentation(ArrowPhase::Following, true, true, {237, 0, 0}).anchor);
    CHECK_FALSE(arrowFlightPresentation(ArrowPhase::Following, true, false,
                                       {std::numeric_limits<float>::infinity(), 0, 0}).anchor);
    const auto ordinary = arrowFlightPresentation(ArrowPhase::Following, true, false, {30, 0, 0});
    CHECK(ordinary.anchor);
    CHECK(ordinary.animationWeight == 0);
    CHECK_FALSE(ordinary.alignModels);
    const auto middle = arrowFlightPresentation(ArrowPhase::Following, true, false, {67.5f, 0, 0});
    CHECK(middle.animationWeight == doctest::Approx(.5f));
    CHECK_FALSE(middle.alignModels);
    for (float speed : {90.f, 120.f, 237.f, 2370.f}) {
        const auto fast = arrowFlightPresentation(ArrowPhase::Following, true, false, {speed, 0, 0});
        CHECK(fast.anchor);
        CHECK(fast.animationWeight == doctest::Approx(1.f));
        CHECK(fast.alignModels);
    }
    CHECK(arrowAnimationWeight(1.f, .2f, .9f) == doctest::Approx(.9f));
    CHECK(arrowAnimationWeight(.4f, .1f, 0.f) == doctest::Approx(.6f));
    CHECK(arrowAnimationWeight(1.f, .95f, .9f) == doctest::Approx(.95f));
    CHECK(arrowAnimationWeight(1.f, 0.f, 1.f) == 1.f);
}

TEST_CASE("arrow draw translation keeps the glider and equipment attached to the body") {
    const handheld::Matrix body{{0, 0, 1, 1400, 0, 1, 0, 300, -1, 0, 0, -2900}};
    const handheld::Matrix attachment{{1, 0, 0, .1f, 0, 0, -1, 1.8f, 0, 1, 0, .3f}};
    const auto glider = handheld::compose(body, attachment);
    for (float offset : {-8.f, -1.f, 0.f, 2.f, 8.f}) {
        const auto target = add(handheld::position(body), Vec3{offset, -.1f * offset, .2f * offset});
        Vec3 shift{};
        REQUIRE(arrowModelTranslation(body, target, shift));
        const auto movedBody = translateArrowModel(body, shift);
        const auto movedGlider = translateArrowModel(glider, shift);
        CHECK(distance(handheld::position(movedBody), target) < .001f);
        const auto expected = handheld::compose(movedBody, attachment);
        for (unsigned i = 0; i < 12; ++i)
            CHECK(movedGlider.v[i] == doctest::Approx(expected.v[i]));
        const auto again = translateArrowModel(glider, shift);
        for (unsigned i = 0; i < 12; ++i)
            CHECK(again.v[i] == movedGlider.v[i]);
    }
}
TEST_CASE("arrow draw refuses invalid and excessive separation without publishing a shift") {
    handheld::Matrix body{};
    Vec3 shift{1, 2, 3};
    const auto unchanged = [&] { CHECK(shift.x == 1); CHECK(shift.y == 2); CHECK(shift.z == 3); };
    CHECK_FALSE(arrowModelTranslation(body, {13, 0, 0}, shift));
    unchanged();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(arrowModelTranslation(body, {nan, 0, 0}, shift));
    unchanged();
    body.v[0] = nan;
    CHECK_FALSE(arrowModelTranslation(body, {}, shift));
    unchanged();
}

TEST_CASE("drawn arrow and player keep the trail distance despite carrier sampling drift") {
    for (const Vec3 velocity : {Vec3{237, 0, 0}, Vec3{0, 237, 0}, Vec3{-120, 36, 200}}) {
        const auto direction = mul(velocity, 1.f / length(velocity));
        for (float carrierError : {-8.f, -3.f, 0.f, 3.f, 8.f}) {
            const Vec3 arrow{1200, 350, -2700};
            auto body = handheld::Matrix{};
            const auto carrier = sub(arrow, mul(direction, 1.25f + carrierError));
            body.v[3] = carrier.x;
            body.v[7] = carrier.y;
            body.v[11] = carrier.z;
            Vec3 target{}, shift{};
            REQUIRE(arrowRenderTranslation(body, arrow, velocity, target, shift));
            const auto drawn = handheld::position(translateArrowModel(body, shift));
            CHECK(distance(drawn, arrow) == doctest::Approx(1.25f).epsilon(.001));
            CHECK(distance(drawn, sub(arrow, mul(direction, 1.25f))) < .001f);
        }
    }
    Vec3 target{1, 2, 3}, shift{4, 5, 6};
    CHECK_FALSE(arrowRenderTranslation({}, {0, 0, 0}, {}, target, shift));
    CHECK_FALSE(arrowRenderTranslation({}, {100, 0, 0}, {237, 0, 0}, target, shift));
    CHECK(target.x == 1); CHECK(target.y == 2); CHECK(target.z == 3);
    CHECK(shift.x == 4); CHECK(shift.y == 5); CHECK(shift.z == 6);
}

TEST_CASE("travel anchor follows rotated translated basis and preserves relative limb motion") {
    const handheld::Matrix basis{{0, 0, 1, 1400, 0, 1, 0, 300, -1, 0, 0, -2900}};
    const Vec3 animated{0.03f, 0.98f, -0.04f};
    const auto target = handheld::point(basis, animated);
    for (float lag : {-12.f, 0.f, 0.1f, 2.f, 8.f, 40.f}) {
        const auto physical = sub(target, Vec3{lag, 0.2f * lag, -0.3f * lag});
        const auto foot = add(physical, Vec3{0.2f, -0.8f, 0.4f});
        Vec3 cached{}, shift{};
        REQUIRE(travelBodyAnchor(basis, animated, physical, cached, shift));
        const auto nativeShift = sub(handheld::position(basis), cached);
        CHECK(distance(add(physical, nativeShift), target) < 0.001f);
        CHECK(distance(sub(add(foot, nativeShift), add(physical, nativeShift)),
                       sub(foot, physical)) < 0.001f);
    }
}
TEST_CASE("arrow draw keeps one target when render advance crosses twelve metres") {
    const Vec3 velocity{237, 0, 0};
    const float lag[]{10.32f, 10.43f, 12.2f, 12.8f, 11.82f, 10.1f};
    Vec3 previous{};
    for (unsigned i = 0; i < 6; ++i) {
        const Vec3 arrow{1200.f + i * 7.9f, 350, -2700};
        auto body = handheld::Matrix{};
        body.v[3] = arrow.x - 1.25f - lag[i];
        body.v[7] = arrow.y;
        body.v[11] = arrow.z;
        Vec3 target{}, shift{};
        REQUIRE(arrowRenderTranslation(body, arrow, velocity, target, shift));
        CHECK(distance(target, arrow) == doctest::Approx(1.25f));
        CHECK(shift.x == doctest::Approx(lag[i]).epsilon(.001));
        if (i) CHECK(target.x - previous.x == doctest::Approx(7.9f).epsilon(.001));
        previous = target;
    }
}
TEST_CASE("arrow camera focus follows the drawn target and keeps the native focus offset") {
    const Vec3 player{1200, 350, -2700};
    for (const Vec3 offset : {Vec3{0, 1.4f, 0}, Vec3{.3f, 1.2f, -.2f}}) {
        const Vec3 nativeFocus = add(player, offset);
        for (const Vec3 delta : {Vec3{18, 2, 0}, Vec3{-18, -2, 0}, Vec3{0, 18, 0}}) {
            const Vec3 target = add(player, delta);
            Vec3 focus{};
            REQUIRE(arrowCameraFocus(nativeFocus, player, target, {237, 0, 0}, focus));
            CHECK(distance(sub(focus, target), offset) < .001f);
        }
        Vec3 focus{};
        REQUIRE(arrowCameraFocus(nativeFocus, player, player, {237, 0, 0}, focus));
        CHECK(distance(focus, nativeFocus) < .001f);
    }
}
TEST_CASE("arrow glider binding follows the corrected animated hand including scale and rotation") {
    const handheld::Matrix oldBone{{0, 0, 2, 1400, 0, 1, 0, 300, -1, 0, 0, -2900}};
    const handheld::Matrix grip{{1, 0, 0, .18f, 0, 1, 0, -.08f, 0, 0, 1, .31f}};
    const auto glider = handheld::compose(oldBone, grip);
    for (const auto& newBone : {
             handheld::Matrix{{1, 0, 0, 1412, 0, 0, -1, 302, 0, 1, 0, -2897}},
             handheld::Matrix{{0, 0, -2, 1406, 0, 1, 0, 300, 1, 0, 0, -2900}},
             oldBone}) {
        handheld::Matrix result{};
        REQUIRE(arrowAttachmentRoot(oldBone, newBone, glider, result));
        const auto expected = handheld::compose(newBone, grip);
        for (unsigned i = 0; i < 12; ++i)
            CHECK(result.v[i] == doctest::Approx(expected.v[i]).epsilon(.0001));
        const Vec3 handle{.2f, 1.f, -.3f};
        CHECK(distance(handheld::point(result, handle), handheld::point(expected, handle)) < .001f);
    }
}
TEST_CASE("arrow glider binding refuses singular and nonfinite transforms without changing output") {
    handheld::Matrix result{}, invalid{};
    invalid.v[0] = 0;
    CHECK_FALSE(arrowAttachmentRoot(invalid, {}, {}, result));
    CHECK(result.v[0] == 1);
    invalid = {};
    invalid.v[7] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(arrowAttachmentRoot({}, invalid, {}, result));
    CHECK_FALSE(arrowAttachmentRoot({}, {}, invalid, result));
    CHECK(result.v[7] == 0);
}
TEST_CASE("repeated arrow scene preparation keeps the glider binding without another simulation update") {
    handheld::Matrix bone{{1, 0, 0, 1200, 0, 1, 0, 351, 0, 0, 1, -2700}};
    const handheld::Matrix binding{{0, 0, 1, .1f, 0, 1, 0, .2f, -1, 0, 0, -.1f}};
    auto glider = handheld::compose(bone, binding);
    for (float step : {12.f, 0.f, 4.f, 0.f, 4.f}) {
        const auto next = translateArrowModel(bone, {step, .1f * step, 0});
        handheld::Matrix updated{};
        REQUIRE(arrowAttachmentRoot(bone, next, glider, updated));
        const auto expected = handheld::compose(next, binding);
        for (unsigned i = 0; i < 12; ++i)
            CHECK(updated.v[i] == doctest::Approx(expected.v[i]).epsilon(.0001));
        bone = next;
        glider = updated;
    }
}
TEST_CASE("arrow camera focus rejects invalid and excessive corrections without writing") {
    Vec3 focus{1, 2, 3};
    const auto unchanged = [&] { CHECK(focus.x == 1); CHECK(focus.y == 2); CHECK(focus.z == 3); };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(arrowCameraFocus({nan, 0, 0}, {}, {}, {237, 0, 0}, focus));
    unchanged();
    CHECK_FALSE(arrowCameraFocus({}, {nan, 0, 0}, {}, {237, 0, 0}, focus));
    unchanged();
    CHECK_FALSE(arrowCameraFocus({}, {}, {nan, 0, 0}, {237, 0, 0}, focus));
    unchanged();
    CHECK_FALSE(arrowCameraFocus({}, {}, {}, {nan, 0, 0}, focus));
    unchanged();
    CHECK_FALSE(arrowCameraFocus({}, {}, {100, 0, 0}, {237, 0, 0}, focus));
    unchanged();
    CHECK_FALSE(arrowModelTranslation({}, {}, focus, nan));
    unchanged();
}
TEST_CASE("travel anchor rejects invalid pose data") {
    Vec3 cached{}, shift{};
    handheld::Matrix basis{};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(travelBodyAnchor(basis, {nan, 0, 0}, {}, cached, shift));
    CHECK_FALSE(travelBodyAnchor(basis, {}, {0, nan, 0}, cached, shift));
    basis.v[0] = nan;
    CHECK_FALSE(travelBodyAnchor(basis, {}, {}, cached, shift));
}

TEST_CASE("body read shift is confined to the merge thread and owned bodies") {
    TravelBodyReadShift correction{};
    correction.thread = 11;
    correction.bodies[0] = 21;
    correction.bodies[1] = 22;
    correction.count = 2;
    correction.shift = {8, -3, 5};
    const handheld::Matrix original{{0, 0, 1, 1400, 0, 1, 0, 300, -1, 0, 0, -2900}};
    auto unrelated = original;
    CHECK_FALSE(correction.apply(12, 21, &unrelated));
    CHECK_FALSE(correction.apply(11, 23, &unrelated));
    CHECK_FALSE(correction.apply(11, 0, &unrelated));
    CHECK_FALSE(correction.apply(11, 21, nullptr));
    CHECK(distance(handheld::position(unrelated), handheld::position(original)) == 0);
    auto hip = original, foot = original;
    foot.v[7] -= .8f;
    const auto difference = sub(handheld::position(foot), handheld::position(hip));
    REQUIRE(correction.apply(11, 21, &hip));
    REQUIRE(correction.apply(11, 22, &foot));
    CHECK(distance(handheld::position(hip), add(handheld::position(original), correction.shift)) <
          .001f);
    CHECK(distance(sub(handheld::position(foot), handheld::position(hip)), difference) < .001f);
    for (unsigned i : {0u, 1u, 2u, 4u, 5u, 6u, 8u, 9u, 10u})
        CHECK(hip.v[i] == original.v[i]);
    correction = {};
    CHECK_FALSE(correction.apply(11, 21, &hip));
}

TEST_CASE("body read shift refuses invalid inputs before changing the matrix") {
    TravelBodyReadShift correction{};
    correction.thread = 11;
    correction.bodies[0] = 21;
    correction.count = 1;
    handheld::Matrix matrix{};
    correction.shift.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(correction.apply(11, 21, &matrix));
    CHECK(matrix.v[3] == 0);
    correction.shift = {};
    correction.count = 65;
    CHECK_FALSE(correction.apply(11, 21, &matrix));
    correction.count = 1;
    matrix.v[7] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(correction.apply(11, 21, &matrix));
    CHECK(matrix.v[3] == 0);
}
