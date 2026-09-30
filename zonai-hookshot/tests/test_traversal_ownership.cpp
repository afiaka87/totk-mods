// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <initializer_list>
#include "TraversalOwnership.hpp"

using namespace zonai_hookshot::pure;

TEST_CASE("idle bow and chord intents reserve only one traversal") {
    auto d = traversalOwnership({false, false, true, false, false, false});
    CHECK(d.allowManual);
    CHECK(d.allowArrow);
    d = traversalOwnership({false, false, true, false, true, false});
    CHECK(d.allowManual);
    CHECK_FALSE(d.allowArrow);
    d = traversalOwnership({false, false, true, true, true, false});
    CHECK_FALSE(d.allowManual);
    CHECK(d.allowArrow);
}

TEST_CASE("drawing the bow releases any manual phase before arrow admission") {
    auto d = traversalOwnership({true, false, true, true, false, false});
    CHECK(d.yieldManual);
    CHECK_FALSE(d.yieldArrow);
    CHECK_FALSE(d.allowManual);
    CHECK(d.allowArrow);
    d = traversalOwnership({true, false, true, false, false, false});
    CHECK_FALSE(d.yieldManual);
    CHECK_FALSE(d.allowArrow);
}

TEST_CASE("manual chord retires following or pending arrow before arming") {
    auto d = traversalOwnership({false, true, true, false, true, false});
    CHECK(d.yieldArrow);
    CHECK(d.allowManual);
    CHECK_FALSE(d.allowArrow);
    d = traversalOwnership({false, true, true, true, true, false});
    CHECK_FALSE(d.yieldArrow);
    CHECK_FALSE(d.allowManual);
    CHECK(d.allowArrow);
}

TEST_CASE("cancel and stale samples cannot transfer ownership") {
    for (bool manual : {false, true}) {
        for (bool fresh : {false, true}) {
            auto d = traversalOwnership({manual, !manual, fresh, true, true, true});
            CHECK_FALSE(d.yieldManual);
            CHECK_FALSE(d.yieldArrow);
            CHECK_FALSE(d.allowManual);
            CHECK(d.allowArrow == !manual);
        }
    }
    auto d = traversalOwnership({true, false, false, true, false, false});
    CHECK_FALSE(d.yieldManual);
    CHECK_FALSE(d.allowArrow);
    d = traversalOwnership({false, true, false, false, true, false});
    CHECK_FALSE(d.yieldArrow);
    CHECK_FALSE(d.allowManual);
}

TEST_CASE("an existing owner excludes the other feature for all input combinations") {
    for (int bits = 0; bits < 16; ++bits) {
        for (bool manual : {false, true}) {
            const auto d = traversalOwnership({manual, !manual,
                (bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, (bits & 8) != 0});
            CHECK_FALSE(((manual && !d.yieldManual) && d.allowArrow));
            CHECK_FALSE(((!manual && !d.yieldArrow) && d.allowManual));
            CHECK_FALSE((d.yieldManual && d.yieldArrow));
        }
    }
}

TEST_CASE("host dispatch retires movement before either new owner can run") {
    for (bool startManual : {false, true}) {
        bool manualDrive = startManual;
        bool arrowDrive = !startManual;
        int order = 0;
        const auto d = traversalOwnership({startManual, !startManual, true,
                                          startManual, !startManual, false});
        dispatchTraversal(d,
            [&] { REQUIRE(order == 0); manualDrive = false; ++order; },
            [&] { REQUIRE(order == 0); arrowDrive = false; ++order; },
            [&](bool allowed) {
                REQUIRE(order == 1);
                if (allowed) { CHECK_FALSE(manualDrive); arrowDrive = true; }
                ++order;
            },
            [&](bool allowed) {
                REQUIRE(order == 2);
                if (allowed) { CHECK_FALSE(arrowDrive); manualDrive = true; }
                ++order;
            });
        CHECK(order == 3);
        CHECK(manualDrive == !startManual);
        CHECK(arrowDrive == startManual);
    }
}

TEST_CASE("arrow cancellation masks B before the manual tracker reads it") {
    bool rawB = true;
    int ticks = 0;
    dispatchTraversal(traversalOwnership({false, true, true, false, true, rawB}),
        [] { FAIL("manual must not yield on B"); },
        [] { FAIL("arrow must process cancellation instead of yielding"); },
        [&](bool allowed) { CHECK(allowed); rawB = false; ++ticks; },
        [&](bool allowed) { CHECK_FALSE(allowed); CHECK_FALSE(rawB); ++ticks; });
    CHECK(ticks == 2);
}
