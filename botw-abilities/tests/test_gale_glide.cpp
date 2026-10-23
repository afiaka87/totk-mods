// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "GaleGlide.hpp"
using namespace botw_abilities::champions;

TEST_CASE("Gale opens only its launched player's glider and hands control back on entry") {
    GaleGlide gate;
    const GlideOwner owner{1,2,3};
    CHECK_FALSE(gate.admits(owner,true,false,true));
    gate.arm(owner);
    CHECK(gate.admits(owner,true,false,true));
    CHECK_FALSE(gate.admits({4,2,3},true,false,true));
    CHECK_FALSE(gate.admits({1,4,3},true,false,true));
    CHECK_FALSE(gate.admits({1,2,4},true,false,true));
    CHECK_FALSE(gate.admits(owner,false,false,true));
    CHECK_FALSE(gate.admits(owner,true,true,true));
    CHECK_FALSE(gate.admits(owner,true,false,false));
    CHECK_FALSE(gate.entered(4));
    CHECK(gate.entered(1));
    CHECK_FALSE(gate.admits(owner,true,false,true));
}

TEST_CASE("Gale glider handoff expires without retrying after a refusal or scene change") {
    GaleGlide gate;
    const GlideOwner owner{1,2,3};
    gate.arm(owner);
    CHECK_FALSE(gate.advance(owner,1));
    CHECK(gate.admits(owner,true,false,true));
    CHECK(gate.advance(owner,2));
    CHECK_FALSE(gate.admits(owner,true,false,true));
    gate.arm(owner);
    CHECK_FALSE(gate.advance({1,3,3},0));
    CHECK_FALSE(gate.admits(owner,true,false,true));
    gate.arm(owner);
    gate.reset();
    CHECK_FALSE(gate.entered(1));
}

namespace {
struct Pulses { int jumps{}, glides{}, pressed{}; };
Pulses run(GaleGlide& gate, int samples, bool grounded) {
    Pulses result;
    for (int i = 0; i < samples; ++i) {
        const auto sample = gate.injectJump(true,false,grounded);
        if (sample.press) ++result.pressed;
        if (sample.started == GlidePulse::Jump) ++result.jumps;
        if (sample.started == GlidePulse::Glide) ++result.glides;
    }
    return result;
}
} // namespace

TEST_CASE("Gale launch waits for X to be released, holds its press, and never presses over a held X") {
    GaleGlide gate;
    const GlideOwner owner{1,2,3};
    CHECK_FALSE(gate.injectJump(true,false,true).press);
    gate.arm(owner);
    CHECK_FALSE(gate.injectJump(false,false,true).press);
    for (int i = 1; i < GaleGlide::kReleaseSamples; ++i) CHECK_FALSE(gate.injectJump(true,false,true).press);
    CHECK_FALSE(gate.injectJump(true,true,true).press);
    for (int i = 1; i < GaleGlide::kReleaseSamples; ++i) CHECK_FALSE(gate.injectJump(true,false,true).press);
    const auto jump = gate.injectJump(true,false,true);
    CHECK(jump.press);
    CHECK(jump.started == GlidePulse::Jump);
    for (int i = 1; i < GaleGlide::kPressSamples; ++i) {
        const auto held = gate.injectJump(true,false,true);
        CHECK(held.press);
        CHECK(held.started == GlidePulse::None);
    }
    CHECK_FALSE(gate.injectJump(true,false,true).press);
    CHECK(gate.claimJump());
    CHECK_FALSE(gate.claimJump());
    CHECK(gate.admits(owner,true,false,true));
}

TEST_CASE("Gale retries a jump the ground swallowed but never opens the glider on the ground") {
    GaleGlide gate;
    gate.arm({1,2,3});
    const auto grounded = run(gate,60,true);
    CHECK(grounded.jumps == GaleGlide::kJumpPresses);
    CHECK(grounded.glides == 0);
    CHECK(grounded.pressed == GaleGlide::kJumpPresses*GaleGlide::kPressSamples);
    CHECK(gate.claimJump());
    CHECK(gate.claimJump());
    CHECK_FALSE(gate.claimJump());
}

TEST_CASE("Once airborne after its jump, Gale presses X again to open the glider") {
    GaleGlide gate;
    gate.arm({1,2,3});
    CHECK(run(gate,GaleGlide::kReleaseSamples,true).jumps == 1);
    const auto air = run(gate,60,false);
    CHECK(air.jumps == 0);
    CHECK(air.glides == GaleGlide::kGlidePresses);
    CHECK(air.pressed == GaleGlide::kPressSamples-1+GaleGlide::kGlidePresses*GaleGlide::kPressSamples);
    CHECK(gate.claimJump());
    CHECK_FALSE(gate.claimJump());
}

TEST_CASE("Gale does not open the glider before its own jump leaves the ground") {
    GaleGlide gate;
    gate.arm({1,2,3});
    CHECK(run(gate,GaleGlide::kReleaseSamples-1,false).pressed == 0);
    const auto first = gate.injectJump(true,false,false);
    CHECK(first.started == GlidePulse::Jump);
}

TEST_CASE("An unconsumed Gale jump pulse cannot swallow a later player jump") {
    GaleGlide gate;
    const GlideOwner owner{1,2,3};
    gate.arm(owner);
    CHECK(gate.advance(owner,3));
    CHECK(run(gate,30,true).pressed == 0);
    CHECK_FALSE(gate.claimJump());
    gate.arm(owner);
    CHECK(gate.entered(1));
    CHECK(run(gate,30,false).pressed == 0);
    CHECK_FALSE(gate.claimJump());
    gate.arm({0,2,3});
    CHECK(run(gate,30,true).pressed == 0);
    CHECK_FALSE(gate.claimJump());
}
