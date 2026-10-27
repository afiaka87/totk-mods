// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "Combat.hpp"

namespace {
using namespace botw_abilities::champions;
constexpr Context live{true};
}

TEST_CASE("Daruk follows ZL without requiring a native shield guard or parry") {
    const auto daruk = initial(Blessing::Daruk);
    CHECK_FALSE(canBlock(daruk, live, false, {2, 1, 1}));
    CHECK(canBlock(daruk, live, true, {2, 1, 1}));
    CHECK(canBlock(daruk, live, true, {2, 0, 0, 8}));
}

TEST_CASE("Native guard slots still select shield wear independently of Daruk activation") {
    CHECK(guardSlot(0x800) == 0);
    CHECK(guardSlot(0x400) == 0);
    CHECK(guardSlot(0x1000) == 4);
    CHECK(guardSlot(0x2000) == 4);
    CHECK(guardSlot(0x3C00) == 0);
    CHECK(guardSlot(0) == 8);
}

TEST_CASE("Cleared contacts and native refusals do not consume a Daruk charge") {
    const auto daruk = initial(Blessing::Daruk);
    for (const IncomingHit hit : {IncomingHit{}, {0, 1, 1, 8}, {2, 0, 1}, {2, 1, 0}})
        CHECK_FALSE(canBlock(daruk, live, true, hit));
    CHECK_FALSE(canBlock(daruk, {true, true}, true, {2, 1, 1}));
    CHECK_FALSE(canBlock(daruk, {}, true, {2, 1, 1}));
    CHECK_FALSE(canBlock({0, 1080, true}, live, true, {2, 1, 1}));
    CHECK_FALSE(canBlock({6, 0, false}, live, true, {2, 1, 1}));
    CHECK(daruk.charges == 6);
}

TEST_CASE("Urbosa accepts both completed charge levels and rejects an uncharged quick spin") {
    const auto urbosa = initial(Blessing::Urbosa);
    CHECK_FALSE(canReleaseFury(urbosa, live, 0, true));
    CHECK(canReleaseFury(urbosa, live, 1, true));
    CHECK(canReleaseFury(urbosa, live, 2, true));
    CHECK_FALSE(canReleaseFury(urbosa, live, 3, true));
    CHECK_FALSE(canReleaseFury(urbosa, live, 1, false));
    CHECK_FALSE(canReleaseFury(urbosa, {true, true}, 1, true));
    CHECK_FALSE(canReleaseFury({0, 720, true}, live, 1, true));
    CHECK_FALSE(canReleaseFury({3, 0, false}, live, 1, true));
    CHECK(urbosa.charges == 3);
}
