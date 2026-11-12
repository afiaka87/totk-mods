// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "../src/wolf/CircleMove.hpp"
#include <cmath>
#include <optional>
#include "doctest.h"

using namespace botw_abilities::wolf;
using totk::core::WorldPosition;

namespace {
constexpr WorldPosition kWolf{0, 0, 0}, kEnemy{0, 0, 10}, kFacingRight{1, 0, 0}, kFacingLeft{-1, 0, 0};

std::optional<WorldPosition> anyGround(const WorldPosition& point) { return point; }
std::optional<WorldPosition> noGround(const WorldPosition&) { return std::nullopt; }

float flat(const WorldPosition& a, const WorldPosition& b) { return std::hypot(a.x - b.x, a.z - b.z); }
}

TEST_CASE("The circling wolf aims five units ahead along the circle, the way he already faces") {
    for (const auto& facing : {kFacingRight, kFacingLeft}) {
        CircleMove circle;
        circle.begin(kInnerCircle);
        const auto aim = circle.update(kWolf, facing, kEnemy, 1, anyGround);
        REQUIRE(aim);
        CHECK(flat(*aim, kWolf) == doctest::Approx(CircleMove::kStep));
        CHECK(aim->x * facing.x > 0);
        CHECK(aim->y == 0);
    }
}

TEST_CASE("The aim is picked again only every second tick") {
    CircleMove circle;
    circle.begin(kInnerCircle);
    const auto first = circle.update(kWolf, kFacingRight, kEnemy, 1, anyGround);
    const auto held = circle.update({1, 0, 0}, kFacingRight, kEnemy, 1, anyGround);
    REQUIRE(first);
    REQUIRE(held);
    CHECK(held->x == first->x);
    const auto next = circle.update({1, 0, 0}, kFacingRight, kEnemy, 1, anyGround);
    REQUIRE(next);
    CHECK(next->x != first->x);
}

TEST_CASE("The circle tightens 0.02 a tick from its start range and stops within 0.95 of its end range") {
    CircleMove circle;
    circle.begin(kInnerCircle);
    CHECK(circle.radius() == doctest::Approx(10.5f));
    for (int i = 0; i < 100; ++i) static_cast<void>(circle.update(kWolf, kFacingRight, kEnemy, 1, anyGround));
    CHECK(circle.radius() == doctest::Approx(8.5f).epsilon(0.001));
    for (int i = 0; i < 400; ++i) static_cast<void>(circle.update(kWolf, kFacingRight, kEnemy, 1, anyGround));
    CHECK(circle.radius() <= 6 * CircleMove::kEndSlack);
    CHECK(circle.radius() > 6 * CircleMove::kEndSlack - 0.021f);
}

TEST_CASE("Every 100 to 160 ticks the wolf pauses 20 to 25 ticks and aims back the other way") {
    CircleMove circle(9);
    circle.begin(kInnerCircle);
    int ticks = 0;
    while (!circle.pausing() && ticks < 200) {
        static_cast<void>(circle.update(kWolf, kFacingRight, kEnemy, 1, anyGround));
        ++ticks;
    }
    CHECK(ticks >= 100);
    CHECK(ticks <= 161);
    std::optional<WorldPosition> aim;
    for (int i = 0; i < 2; ++i) aim = circle.update(kWolf, kFacingRight, kEnemy, 1, anyGround);
    REQUIRE(aim);
    CHECK(aim->x < 0);
    int paused = 2;
    while (circle.pausing() && paused < 60) {
        static_cast<void>(circle.update(kWolf, kFacingRight, kEnemy, 1, anyGround));
        ++paused;
    }
    CHECK(paused >= 19);
    CHECK(paused <= 26);
    for (int i = 0; i < 2; ++i) aim = circle.update(kWolf, kFacingRight, kEnemy, 1, anyGround);
    REQUIRE(aim);
    CHECK(aim->x > 0);
}

TEST_CASE("With no ground ahead the wolf aims the other way round, and with none either way he has no aim") {
    CircleMove circle;
    circle.begin(kInnerCircle);
    const auto rightOnly = [](const WorldPosition& point) -> std::optional<WorldPosition> {
        if (point.x > 0) return point;
        return std::nullopt;
    };
    const auto aim = circle.update(kWolf, kFacingLeft, kEnemy, 1, rightOnly);
    REQUIRE(aim);
    CHECK(aim->x > 0);
    CircleMove blocked;
    blocked.begin(kInnerCircle);
    CHECK_FALSE(blocked.update(kWolf, kFacingRight, kEnemy, 1, noGround));
}

TEST_CASE("The aim takes the height of the ground found under it") {
    CircleMove circle;
    circle.begin(kLeaveCircle);
    const auto raised = [](const WorldPosition& point) -> std::optional<WorldPosition> {
        return WorldPosition{point.x, 2.5f, point.z};
    };
    const auto aim = circle.update(kWolf, kFacingRight, kEnemy, 1, raised);
    REQUIRE(aim);
    CHECK(aim->y == 2.5f);
}
