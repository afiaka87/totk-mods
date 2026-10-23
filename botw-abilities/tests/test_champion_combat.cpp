// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "Combat.hpp"

namespace {
using namespace botw_abilities::champions;
constexpr Context live{true};
}

TEST_CASE("Daruk follows raised guard and parry animation flags, not merely ZL") {
    const auto daruk = initial(Blessing::Daruk);
    for (unsigned bit = 0; bit < 64; ++bit) {
        const auto flags = std::uint64_t{1} << bit;
        CHECK(guardingFromEquipment(flags) == (bit >= 10 && bit <= 13));
    }
    CHECK_FALSE(canBlock(daruk, live, 0, {2, 1, 1}));
    CHECK(canBlock(daruk, live, 0x800, {2, 1, 1}));
    CHECK(canBlock(daruk, live, 0x1000, {2, 0, 0, 8}));
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
        CHECK_FALSE(canBlock(daruk, live, 0x800, hit));
    CHECK_FALSE(canBlock(daruk, {true, true}, 0x800, {2, 1, 1}));
    CHECK_FALSE(canBlock(daruk, {}, 0x800, {2, 1, 1}));
    CHECK_FALSE(canBlock({0, 1080, true}, live, 0x800, {2, 1, 1}));
    CHECK_FALSE(canBlock({6, 0, false}, live, 0x800, {2, 1, 1}));
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
