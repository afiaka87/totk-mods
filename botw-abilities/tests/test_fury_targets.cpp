// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "FuryTargets.hpp"
#include "FuryHits.hpp"
#include <limits>
using namespace botw_abilities::champions;

TEST_CASE("Fury targets enemies within its sphere without hitting allies or distant elevations") {
    const totk::core::WorldPosition center{100,20,-50};
    CHECK(furyTargetInRange(center,{150,20,-50},true));
    CHECK_FALSE(furyTargetInRange(center,{150.1f,20,-50},true));
    CHECK(furyTargetInRange(center,{100,-30,-50},true));
    CHECK_FALSE(furyTargetInRange(center,{100,-31,-50},true));
    CHECK_FALSE(furyTargetInRange(center,{100,20,-50},false));
    CHECK_FALSE(furyTargetInRange(center,{140,60,-50},true));
    CHECK_FALSE(furyTargetInRange(center,{100,20,std::numeric_limits<float>::quiet_NaN()},true));
    CHECK_FALSE(furyTargetInRange(center,center,true,0));
}

TEST_CASE("Fury preserves original damage differences and excludes helpers and Talus bodies") {
    CHECK(furyBalance("Enemy_Bokoblin_Junior") == FuryBalance{150,300});
    CHECK(furyBalance("Enemy_Lizalfos_Electric") == FuryBalance{75,0});
    CHECK(furyBalance("Enemy_Lynel_Senior") == FuryBalance{500,0});
    CHECK(furyBalance("Enemy_Giant_Middle") == FuryBalance{300,150});
    CHECK(furyBalance("Enemy_Golem_WeakPoint") == FuryBalance{300,0});
    CHECK_FALSE(furyBalance("Enemy_Golem_Junior"));
    CHECK_FALSE(furyBalance("Enemy_Dragon_Electric"));
    CHECK_FALSE(furyBalance("Enemy_Assassin_Senior"));
    CHECK_FALSE(furyBalance("Enemy_DungeonBoss_Zora_Bullet_Shark"));
    CHECK_FALSE(furyBalance("Enemy_Drake_Attention"));
    CHECK_FALSE(furyBalance("Player"));
    CHECK_FALSE(furyBalance("Enemy_Unknown"));
}

TEST_CASE("Fury damage and reaction are separately claimed once in either worker order") {
    FuryHits<2> hits;
    const FuryTarget a{0x10000,17}, b{0x20000,18};
    REQUIRE(hits.insert(a,{150,300},60));
    CHECK_FALSE(hits.insert(a,{150,300},60));
    CHECK(hits.takeReaction(a) == FuryBalance{150,300});
    CHECK_FALSE(hits.takeReaction(a));
    CHECK(hits.takeDamage(a) == FuryBalance{150,300});
    CHECK_FALSE(hits.takeDamage(a));
    REQUIRE(hits.insert(b,{300,150},60));
    CHECK(hits.takeDamage(b) == FuryBalance{300,150});
    CHECK(hits.takeReaction(b) == FuryBalance{300,150});
}

TEST_CASE("Fury never delivers an old hit to a reused actor address") {
    FuryHits<1> hits;
    const FuryTarget old{0x10000,17}, reused{0x10000,18};
    REQUIRE(hits.insert(old,{500,0},60));
    CHECK_FALSE(hits.takeDamage(reused));
    CHECK_FALSE(hits.takeReaction(old));
    CHECK_FALSE(hits.insert(reused,{150,300},90));
    CHECK(hits.expire(59) == 0);
    CHECK(hits.expire(60) == 1);
    REQUIRE(hits.insert(reused,{150,300},90));
    hits.clear();
    CHECK_FALSE(hits.takeDamage(reused));
    CHECK_FALSE(hits.takeReaction(reused));
}
