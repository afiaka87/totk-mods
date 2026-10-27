// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "FuryRelease.hpp"
#include "UseEvents.hpp"
#include "HeavyFury.hpp"
#include <limits>

using namespace botw_abilities::champions;
TEST_CASE("Heavy Fury fires once after half a wheel regardless of purchased stamina") {
    for (float total : {1000.0f, 2000.0f, 3000.0f, 5000.0f}) {
        HeavyFury spin;
        const HeavyFuryKey key{1,2,3,4};
        REQUIRE(spin.begin(key,total));
        CHECK_FALSE(spin.charging());
        CHECK_FALSE(spin.advance(key,total-499,true));
        CHECK_FALSE(spin.charging());
        CHECK(spin.advance(key,total-500,true));
        CHECK(spin.charging());
        CHECK_FALSE(spin.release(key,true));
        CHECK_FALSE(spin.advance(key,total-600,true));
        CHECK_FALSE(spin.advance(key,0,true));
        CHECK(spin.release(key,false));
        CHECK_FALSE(spin.charging());
        CHECK_FALSE(spin.release(key,false));
    }
}

TEST_CASE("Heavy Fury uses actual stamina loss rather than update count or charging speed") {
    const HeavyFuryKey key{1,2,3,4};
    for (float step : {2.5f,5.0f,10.0f,20.0f}) {
        HeavyFury spin;
        REQUIRE(spin.begin(key,1000));
        for (float spent=step; spent<500; spent+=step)
            CHECK_FALSE(spin.advance(key,1000-spent,true));
        CHECK(spin.advance(key,500,true));
    }
    HeavyFury spin;
    REQUIRE(spin.begin(key,1000));
    for (int i=0;i<1000;++i) CHECK_FALSE(spin.advance(key,1000,true));
    CHECK_FALSE(spin.advance(key,501,true));
    CHECK(spin.advance(key,490,true));
}

TEST_CASE("Heavy Fury counts drains across recovery without counting bonus stamina transfer twice") {
    HeavyFury spin;
    const HeavyFuryKey key{1,2,3,4};
    REQUIRE(spin.begin(key,1500));
    CHECK_FALSE(spin.advance(key,1250,true));
    CHECK_FALSE(spin.advance(key,1500,true));
    CHECK_FALSE(spin.advance(key,1500,true));
    CHECK(spin.advance(key,1250,true));
}

TEST_CASE("Heavy Fury cancellation and fresh attacks cannot share accumulated stamina") {
    HeavyFury spin;
    const HeavyFuryKey key{1,2,3,4};
    REQUIRE(spin.begin(key,1000));
    CHECK_FALSE(spin.advance(key,600,true));
    spin.end(99);
    CHECK_FALSE(spin.charging());
    spin.end(key.action);
    CHECK_FALSE(spin.advance(key,400,true));
    REQUIRE(spin.begin(key,1000));
    CHECK_FALSE(spin.advance(key,600,true));
    CHECK(spin.advance(key,500,true));
    REQUIRE(spin.begin(key,1000));
    CHECK_FALSE(spin.advance(key,600,true));
    spin.reset();
    CHECK_FALSE(spin.advance(key,400,true));
}

TEST_CASE("Heavy Fury rejects stale identity disabled contexts and invalid stamina") {
    const HeavyFuryKey key{1,2,3,4};
    for (const auto stale : {HeavyFuryKey{9,2,3,4},HeavyFuryKey{1,9,3,4},
                             HeavyFuryKey{1,2,9,4},HeavyFuryKey{1,2,3,9}}) {
        HeavyFury spin;
        REQUIRE(spin.begin(key,1000));
        CHECK_FALSE(spin.advance(stale,0,true));
        CHECK(spin.advance(key,500,true));
    }
    for (float bad : {-1.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        HeavyFury spin;
        CHECK_FALSE(spin.begin(key,bad));
        REQUIRE(spin.begin(key,1000));
        CHECK_FALSE(spin.advance(key,bad,true));
        CHECK_FALSE(spin.advance(key,0,true));
    }
    HeavyFury spin;
    REQUIRE(spin.begin(key,1000));
    CHECK_FALSE(spin.advance(key,500,false));
    CHECK_FALSE(spin.advance(key,0,true));
    REQUIRE(spin.begin(key,400));
    CHECK_FALSE(spin.advance(key,0,true));
}

TEST_CASE("Heavy Fury arms after half a wheel spent from a partially depleted start") {
    HeavyFury spin;
    const HeavyFuryKey key{1,2,3,4};
    // Two wheels, with 75 percent of the first already used before this attack.
    REQUIRE(spin.begin(key,1250));
    CHECK_FALSE(spin.release(key,false));
    CHECK_FALSE(spin.advance(key,1000,true));
    CHECK_FALSE(spin.charging());
    CHECK_FALSE(spin.advance(key,751,true));
    CHECK(spin.advance(key,750,true));
    CHECK(spin.charging());
    CHECK_FALSE(spin.release(key,true));
    CHECK_FALSE(spin.advance(key,600,true));
    CHECK(spin.charging());
    CHECK(spin.release(key,false));
    CHECK_FALSE(spin.release(key,false));
}

TEST_CASE("Heavy Fury armed state cannot release after cancellation or for another identity") {
    const HeavyFuryKey key{1,2,3,4};
    for (const auto stale : {HeavyFuryKey{9,2,3,4},HeavyFuryKey{1,9,3,4},
                             HeavyFuryKey{1,2,9,4},HeavyFuryKey{1,2,3,9}}) {
        HeavyFury spin;
        REQUIRE(spin.begin(key,1000));
        REQUIRE(spin.advance(key,500,true));
        CHECK_FALSE(spin.release(stale,false));
        spin.end(key.action);
        CHECK_FALSE(spin.release(key,false));
        CHECK_FALSE(spin.charging());
    }
}

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
