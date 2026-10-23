// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <initializer_list>
#include "StasisRenderRoute.hpp"
using namespace stasis::render;

TEST_CASE("native Stasis draws world before bloom and HUD before mask release") {
    for (unsigned phase = 0; phase != 4; ++phase) {
        const auto route = nativeSchedule(phase, 0, 0);
        CHECK(route.afterWorld == (phase == 1));
        CHECK(route.beforeHud == (phase == 3));
        CHECK_FALSE((route.afterWorld && route.beforeHud));
        for (const auto ignored : {nativeSchedule(phase, 1, 0), nativeSchedule(phase, 0, 1)}) {
            CHECK_FALSE(ignored.afterWorld);
            CHECK_FALSE(ignored.beforeHud);
        }
    }
}
