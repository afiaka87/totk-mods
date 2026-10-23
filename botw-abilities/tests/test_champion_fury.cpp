// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "FuryRelease.hpp"
#include "UseEvents.hpp"

using namespace botw_abilities::champions;
namespace {
States freshFuryStates() {
    States states;
    for (unsigned i = 0; i < states.size(); ++i) states[i] = initial(static_cast<Blessing>(i));
    return states;
}
}

TEST_CASE("Fury spends one charge per accepted strike and refunds a refused native spawn") {
    SavedState saved;
    REQUIRE(saved.restore(0,freshFuryStates()));
    unsigned attempts = 0;
    const auto refused = releaseFury(saved,0,{true},[&] { ++attempts; return false; });
    CHECK_FALSE(refused);
    CHECK(refused.claim);
    CHECK(refused.committed);
    CHECK(saved.snapshot().states[3].charges == 3);
    for (unsigned i = 0; i < 3; ++i) {
        const auto result = releaseFury(saved,0,{true},[&] { ++attempts; return true; });
        REQUIRE(result);
        CHECK(result.claim.use.state.charges == 2-i);
        CHECK(result.claim.use.state.remainingSeconds == (i == 2 ? 720.0f : 0.0f));
    }
    CHECK_FALSE(releaseFury(saved,0,{true},[&] { ++attempts; return true; }));
    CHECK(attempts == 4);
}

TEST_CASE("Fury never invokes native creation while paused in shrines disabled or on a stale save") {
    SavedState saved;
    REQUIRE(saved.restore(0,freshFuryStates()));
    unsigned attempts = 0;
    auto spawn = [&] { ++attempts; return true; };
    CHECK_FALSE(releaseFury(saved,0,{},spawn));
    CHECK_FALSE(releaseFury(saved,0,{true,true},spawn));
    REQUIRE(saved.enable(0,Blessing::Urbosa,false));
    CHECK_FALSE(releaseFury(saved,0,{true},spawn));
    const auto generation = saved.invalidate();
    REQUIRE(saved.restore(generation,freshFuryStates()));
    CHECK_FALSE(releaseFury(saved,0,{true},spawn));
    CHECK(attempts == 0);
    const auto switchedDuringSpawn = releaseFury(saved,generation,{true},[&] {
        const auto next = saved.invalidate();
        CHECK(saved.restore(next,freshFuryStates()));
        return true;
    });
    CHECK(switchedDuringSpawn.spawned);
    CHECK_FALSE(switchedDuringSpawn.committed);
    CHECK_FALSE(switchedDuringSpawn);
    CHECK(saved.snapshot().states[3].charges == 3);
}

TEST_CASE("Fury release queue retains the attack position and drops old save and scene requests") {
    UseEvents<3,FuryRequest> requests;
    FuryRequest first{1,42,{}}, second{2,42,{}}, third{2,43,{}};
    third.placement.position = {1,2,3};
    CHECK(requests.push(first));
    CHECK(requests.push(second));
    CHECK(requests.push(third));
    CHECK_FALSE(requests.push(third));
    FuryRequest received;
    REQUIRE(requests.pop(2,43,received));
    CHECK(received.placement.position.x == 1);
    CHECK(received.placement.position.y == 2);
    CHECK(received.placement.position.z == 3);
    CHECK_FALSE(requests.pop(2,43,received));
    CHECK(requests.push(third));
    requests.clear();
    CHECK_FALSE(requests.pop(2,43,received));
}
