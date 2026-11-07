// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "../src/wolf/FollowFrame.hpp"

using namespace botw_abilities::wolf;
using totk::core::WorldTransform;

namespace {
constexpr WorldTransform initial{{{1,0,0, 0,1,0, 0,0,1}}, {10,20,30}};
}

TEST_CASE("Wolf follow forced capture takes the leader frame") {
    FollowFrame frame;
    updateFollowFrame(frame, initial, true);
    CHECK(frame.transform.position.x == 10);
    CHECK(frame.anchor.z == 30);
    CHECK(frame.heading == std::array<float, 3>{0,0,1});
}

TEST_CASE("Wolf follow accumulates horizontal travel from its captured frame") {
    FollowFrame frame;
    updateFollowFrame(frame, initial, true);
    auto leader = initial;
    leader.position = {10.5f,500,30};
    updateFollowFrame(frame, leader, false);
    CHECK(frame.transform.position.x == 10);
    CHECK(frame.anchor.y == 20);
    leader.position.x = 11;
    updateFollowFrame(frame, leader, false);
    CHECK(frame.transform.position.x == 11);
    CHECK(frame.transform.position.y == 500);
}

TEST_CASE("Wolf follow turns its heading before recapturing on the next straight update") {
    FollowFrame frame;
    updateFollowFrame(frame, initial, true);
    constexpr WorldTransform turned{{{0,0,1, 0,1,0, -1,0,0}}, {12,20,30}};
    updateFollowFrame(frame, turned, false);
    CHECK(frame.transform.rotation.values[0] == 1);
    CHECK(frame.anchor.x == 10);
    CHECK(frame.heading == std::array<float, 3>{1,0,0});
    updateFollowFrame(frame, turned, false);
    CHECK(frame.transform.rotation.values[0] == 0);
    CHECK(frame.anchor.x == 12);
}
