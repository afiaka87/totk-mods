// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <doctest.h>
#include "../src/wolf/FollowGear.hpp"
#include <vector>
#include <limits>

using namespace botw_abilities::wolf;

TEST_CASE("Wolf path braking uses twice the turning radius minus eight meters") {
    CHECK(pathFollowGear(4, 10, 1, {}, {11.99f, 0, 0}) == 1);
    CHECK(pathFollowGear(4, 10, 1, {}, {12, 0, 0}) == 4);
    CHECK(pathFollowGear(4, 10, 1, {}, {12.01f, 0, 0}) == 4);
    CHECK(pathFollowGear(3, 10, 1, {10, 20, 30}, {10, 31, 30}) == 1);
    CHECK(pathFollowGear(3, 10, 1, {10, 20, 30}, {10, 32, 30}) == 3);
    CHECK(pathFollowGear(2, 4, 1, {}, {}) == 2);
    CHECK(pathFollowGear(2, 3, 1, {}, {}) == 2);
}

TEST_CASE("Wolf path braking preserves the original zero-angular-speed rule") {
    CHECK(pathFollowGear(4, 1, 0, {}, {1000, 0, 0}) == 1);
    CHECK(pathFollowGear(4, 0, 0, {}, {}) == 4);
    const float unset = std::numeric_limits<float>::quiet_NaN();
    CHECK(pathFollowGear(4, 10, 1, {}, {unset, 0, 0}) == 4);
    CHECK(pathFollowGear(4, 10, 1, {}, {0, unset, 0}) == 4);
}

TEST_CASE("Wolf follow entry tries descending speeds without probing the minimum") {
    std::vector<int> probes;
    auto probe = [&](int gear) { probes.push_back(gear); return gear == 2; };
    auto state = beginFollowGear(FollowRange::Far, probe);
    CHECK(probes == std::vector<int>{4, 3, 2});
    CHECK(state.gear == 2);
    CHECK(state.remaining == 30);
    CHECK(state.previous == 30);
    CHECK(state.restricted);
    probes.clear();
    state = beginFollowGear(FollowRange::Near, [&](int gear) { probes.push_back(gear); return false; });
    CHECK(probes == std::vector<int>{2});
    CHECK(state.gear == 1);
}

TEST_CASE("Wolf follow can slow immediately while acceleration is restricted") {
    FollowGear state{30, 30, 4, true};
    std::vector<int> probes;
    auto change = advanceFollowGear(state, FollowRange::Far, 0.5f, [&](int gear) {
        probes.push_back(gear); return false;
    });
    CHECK(probes == std::vector<int>{4});
    CHECK(change.state.gear == 3);
    CHECK(change.state.remaining == 30);
    CHECK(change.state.previous == 30);
    CHECK(change.state.restricted);
    CHECK(change.changed);
    CHECK(change.limitSpeed == true);
    state.gear = 1;
    change = advanceFollowGear(state, FollowRange::Far, 1, [](int) { return false; });
    CHECK_FALSE(change.changed);
    CHECK_FALSE(change.limitSpeed.has_value());
    CHECK(change.state.remaining == 29);
    CHECK(change.state.previous == 30);
}

TEST_CASE("Wolf follow acceleration resumes on the expiry frame and resets only when gear changes") {
    FollowGear state{0.5f, 1.0f, 1, true};
    std::vector<int> probes;
    auto probe = [&](int gear) { probes.push_back(gear); return true; };
    auto change = advanceFollowGear(state, FollowRange::Far, 0.25f, probe);
    CHECK(probes == std::vector<int>{1});
    CHECK_FALSE(change.changed);
    CHECK(change.state.remaining == 0.25f);
    probes.clear();
    change = advanceFollowGear(state, FollowRange::Far, 0.5f, probe);
    CHECK(probes == std::vector<int>{1, 2});
    CHECK(change.state.gear == 2);
    CHECK(change.changed);
    CHECK_FALSE(change.limitSpeed.has_value());
    CHECK(change.state.remaining == 30);
    CHECK(change.state.restricted);
    change = advanceFollowGear(state, FollowRange::Near, 0.5f, probe);
    CHECK(change.limitSpeed == false);
}

TEST_CASE("Wolf follow still queries beyond its maximum before clamping") {
    FollowGear state{0, 1, 4, false};
    std::vector<int> probes;
    const auto change = advanceFollowGear(state, FollowRange::Far, 1, [&](int gear) {
        probes.push_back(gear); return true;
    });
    CHECK(probes == std::vector<int>{4, 5});
    CHECK(change.state.gear == 4);
    CHECK_FALSE(change.changed);
    CHECK_FALSE(change.limitSpeed.has_value());
    CHECK_FALSE(change.state.restricted);
    CHECK(change.state.remaining == 0);
    CHECK(change.state.previous == 1);
}

TEST_CASE("Wolf close follow uses lower distance thresholds when slowing") {
    CHECK(closeFollowGear(2.0f, {1, 1, 0, true}) == 1);
    CHECK(closeFollowGear(2.0f, {2, 2, 0, true}) == 2);
    CHECK(closeFollowGear(1.4f, {2, 2, 0, true}) == 1);
    CHECK(closeFollowGear(5.0f, {2, 2, 0, true}) == 2);
    CHECK(closeFollowGear(5.0f, {3, 3, 0, true}) == 3);
    CHECK(closeFollowGear(9.0f, {3, 3, 0, true}) == 3);
    CHECK(closeFollowGear(9.0f, {4, 4, 0, true}) == 4);
}

TEST_CASE("Wolf close follow honors speed transitions and changes down one gear at a time") {
    CHECK(closeFollowGear(1.0f, {4, 4, 0, true}) == 3);
    CHECK(closeFollowGear(20.0f, {1, 1, 0, true}) == 3);
    CHECK(closeFollowGear(1.0f, {4, 3, 5, true}) == 3);
    CHECK(closeFollowGear(1.0f, {3, 3, 5, false}) == 3);
    CHECK(closeFollowGear(1.0f, {3, 3, 5, true}) == 2);
    CHECK(closeFollowGear(1.0f, {4, 3, 0, true}) == 2);
    CHECK(closeFollowGear(0.0f, {0, 0, 0, true}) == 1);
}
