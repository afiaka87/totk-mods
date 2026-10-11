#include "doctest.h"
#include "StasisTrail.hpp"
TEST_CASE("Released trail follows supplied path, pauses and has a finite lifetime") {
    stasis::Trail trail;
    CHECK_FALSE(trail.visible());
    trail.begin({1, 2, 3});
    CHECK(trail.count == 1);
    trail.advance(0);
    trail.record({8, 9, 10});
    CHECK(trail.count == 1);
    trail.advance(1);
    trail.record({4, 6, 8});
    CHECK(trail.count == 2);
    CHECK(trail.samples[1].position.y == 6);
    CHECK(trail.samples[1].birth == doctest::Approx(1.0f / 30));
    trail.advance(100);
    CHECK_FALSE(trail.emitting());
    CHECK(trail.visible());
    trail.record({9, 9, 9});
    CHECK(trail.count == 2);
    trail.advance(100);
    CHECK_FALSE(trail.visible());
    CHECK(trail.count == 0);
}
TEST_CASE("Released trail bounds history and rejects teleport or invalid samples") {
    stasis::Trail trail;
    trail.begin({0, 0, 0});
    for (unsigned i = 0; i < 110; ++i) {
        trail.advance(.9f);
        trail.record({float(i) * .01f, 0, 0});
    }
    CHECK(trail.count == stasis::kTrailSamples);
    CHECK(trail.samples[0].birth > 0);
    trail.begin({0, 0, 0});
    trail.advance(1);
    trail.record({100, 0, 0});
    CHECK_FALSE(trail.visible());
    trail.begin({0, 0, 0});
    trail.advance(1);
    trail.record({NAN, 0, 0});
    CHECK(trail.count == 1);
    trail.reset();
    CHECK_FALSE(trail.visible());
}
