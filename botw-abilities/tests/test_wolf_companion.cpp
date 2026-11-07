// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/wolf/Companion.hpp"
#include <ostream>
#include <string_view>
#include <utility>
#include "doctest.h"

using namespace botw_abilities::wolf;

namespace {
CompanionInput calm() {
    CompanionInput input;
    input.playerDistance = 3;
    input.life = kMaxLife;
    input.animationDone = true;
    return input;
}
void settle(Companion& wolf, CompanionInput input = calm()) {
    for (int i = 0; i < 12; ++i) static_cast<void>(wolf.step(input));
}
bool plays(Companion& wolf, const CompanionInput& input, std::string_view command) {
    for (int i = 0; i < 3; ++i) {
        const auto output = wolf.step(input);
        if (output.play) return command == output.play;
    }
    return false;
}
CompanionInput enemy(float distance) {
    auto input = calm();
    input.target = {true, false, distance, 0, 0};
    return input;
}
}

TEST_CASE("Wolf Link power and defense match the original hearts formula") {
    CHECK(companionPower(12, false) == 10);
    CHECK(companionPower(80, false) == 15);
    CHECK(companionPower(12, true) == 50);
    CHECK(companionDefense(12) == 5);
    CHECK(companionDefense(80) == 15);
}

TEST_CASE("Summoning plays Spawn then Howling and then follows") {
    Companion wolf;
    wolf.arrive(false);
    auto input = calm();
    input.animationDone = false;
    auto output = wolf.step(input);
    CHECK(std::string_view(output.play) == "Spawn");
    input.animationDone = true;
    output = wolf.step(input);
    CHECK(std::string_view(output.play) == "Howling");
    output = wolf.step(input);
    CHECK(output.move == CompanionMove::Follow);
}

TEST_CASE("The first life reading is replaced by the wolf's own maximum") {
    Companion wolf;
    wolf.arrive(false);
    auto input = calm();
    input.life = 24;
    CHECK(wolf.step(input).setLife == kMaxLife);
}

TEST_CASE("A small hit ends after the original's short reaction and a burn lasts longer") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.animationDone = false;
    input.hit = 1;
    auto output = wolf.step(input);
    CHECK(wolf.mode() == CompanionMode::Reaction);
    CHECK(std::string_view(output.play) == "Damage");
    int ticks = 0;
    input.hit = 0;
    input.life = wolf.life();
    while (wolf.mode() == CompanionMode::Reaction && ticks < 40) {
        static_cast<void>(wolf.step(input));
        ++ticks;
    }
    CHECK(ticks >= 5);
    CHECK(ticks <= 8);

    Companion burned;
    burned.arrive(false);
    settle(burned);
    input = calm();
    input.animationDone = false;
    input.status.burning = true;
    output = burned.step(input);
    CHECK(burned.mode() == CompanionMode::Reaction);
    CHECK(std::string_view(output.play) == "Damage");
    ticks = 0;
    while (burned.mode() == CompanionMode::Reaction && ticks < 40) {
        static_cast<void>(burned.step(input));
        ++ticks;
    }
    CHECK(ticks >= 15);
    CHECK(ticks <= 18);
    settle(burned, [&] { auto again = calm(); again.status.burning = true; return again; }());
    CHECK(burned.mode() == CompanionMode::Follow);
}

TEST_CASE("Ice holds the wolf still with no animation until it thaws") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.status.frozen = true;
    input.stay = true;
    auto output = wolf.step(input);
    CHECK(wolf.mode() == CompanionMode::Frozen);
    CHECK(output.move == CompanionMove::Script);
    CHECK(output.play == nullptr);
    CHECK_FALSE(wolf.ordered());
    input.stay = false;
    for (int i = 0; i < 100; ++i) CHECK(wolf.step(input).play == nullptr);
    input.status.frozen = false;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
}

TEST_CASE("A stuck ice reading cannot park the wolf forever") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.status.frozen = true;
    for (int i = 0; i < 1810; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
    input.status.frozen = false;
    static_cast<void>(wolf.step(input));
    input.status.frozen = true;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Frozen);
}

TEST_CASE("Shock repeats the hurt animation until the charge ends") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.status.shocked = true;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Shocked);
    int plays = 0;
    for (int i = 0; i < 12; ++i) {
        const auto output = wolf.step(input);
        if (output.play && std::string_view(output.play) == "Damage") ++plays;
    }
    CHECK(plays >= 3);
    CHECK(wolf.mode() == CompanionMode::Shocked);
    input.status.shocked = false;
    settle(wolf, input);
    CHECK(wolf.mode() == CompanionMode::Follow);
}

TEST_CASE("A gust knocks the wolf down once for each gust") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.animationDone = false;
    input.status.gust = true;
    auto output = wolf.step(input);
    CHECK(wolf.mode() == CompanionMode::Reaction);
    CHECK(std::string_view(output.play) == "Dead");
    input.animationDone = true;
    output = wolf.step(input);
    CHECK(std::string_view(output.play) == "Standup");
    settle(wolf, input);
    CHECK(wolf.mode() == CompanionMode::Follow);
}

TEST_CASE("Status effects never interrupt arriving, warping or dying") {
    Companion wolf;
    wolf.arrive(false);
    auto input = calm();
    input.animationDone = false;
    input.status.frozen = true;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Appear);
}

namespace {
CompanionInput sensing(float wolfToGoal, float playerToGoal, float playerToWolf) {
    auto input = calm();
    input.playerDistance = playerToWolf;
    input.lead = {true, wolfToGoal, playerToGoal};
    return input;
}
}

TEST_CASE("The wolf leads to the sensor's target and stops once there") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    CHECK(wolf.step(sensing(100, 105, 5)).move == CompanionMove::Lead);
    CHECK(wolf.mode() == CompanionMode::Lead);
    CHECK(wolf.step(sensing(50, 55, 5)).move == CompanionMove::Lead);
    CHECK(wolf.step(sensing(9, 14, 5)).move == CompanionMove::Follow);
    CHECK(wolf.mode() == CompanionMode::Follow);
    CHECK(wolf.step(sensing(9, 14, 5)).move == CompanionMove::Follow);
}

TEST_CASE("The wolf waits at 30 units behind it and resumes at 10") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    CHECK(wolf.step(sensing(100, 105, 5)).move == CompanionMove::Lead);
    CHECK(wolf.step(sensing(100, 129, 29)).move == CompanionMove::Lead);
    CHECK(wolf.step(sensing(99, 130, 31)).move == CompanionMove::Hold);
    CHECK(wolf.step(sensing(99, 118, 20)).move == CompanionMove::Hold);
    CHECK(wolf.step(sensing(99, 109, 10)).move == CompanionMove::Lead);
}

TEST_CASE("The wolf does not wait for a player already nearer the target") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    CHECK(wolf.step(sensing(100, 105, 5)).move == CompanionMove::Lead);
    CHECK(wolf.step(sensing(100, 60, 40)).move == CompanionMove::Lead);
    CHECK(wolf.step(sensing(99, 59, 40)).move == CompanionMove::Lead);
}

TEST_CASE("A wolf far behind a following player takes up no lead, food or fight until back beside him") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto far = calm();
    far.playerDistance = 20;
    far.standing = false;
    static_cast<void>(wolf.step(far));
    auto distracted = enemy(10);
    distracted.playerDistance = 20;
    distracted.lead = {true, 50, 60};
    distracted.food = {true, false, 5, 0, 4};
    for (int i = 0; i < 20; ++i) CHECK(wolf.step(distracted).move == CompanionMove::Follow);
    CHECK(wolf.mode() == CompanionMode::Follow);
    distracted.playerDistance = 5;
    static_cast<void>(wolf.step(distracted));
    CHECK(wolf.mode() != CompanionMode::Follow);
}

TEST_CASE("A wolf that stops short of the player stops catching up") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto behind = enemy(10);
    behind.playerDistance = 12;
    behind.target.present = false;
    auto far = behind;
    far.playerDistance = 20;
    static_cast<void>(wolf.step(far));
    behind.standing = true;
    for (int i = 0; i < 29; ++i) static_cast<void>(wolf.step(behind));
    CHECK(wolf.mode() == CompanionMode::Follow);
    behind.target.present = true;
    for (int i = 0; i < 3; ++i) static_cast<void>(wolf.step(behind));
    CHECK(wolf.mode() != CompanionMode::Follow);
}

TEST_CASE("The wolf turns to face a player it is waiting for") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    static_cast<void>(wolf.step(sensing(100, 105, 5)));
    auto input = sensing(99, 131, 32);
    input.leaderAngle = 2;
    CHECK(plays(wolf, input, "Turn_Notice_NoGrowl"));
    CHECK(wolf.mode() == CompanionMode::Facing);
}

TEST_CASE("A lead that stops closing in gives up and retries later") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    const auto input = sensing(100, 105, 5);
    for (int i = 0; i < 299; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Lead);
    for (int i = 0; i < 3; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
    for (int i = 0; i < 890; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
    for (int i = 0; i < 12; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Lead);
}

TEST_CASE("A momentary sensor dropout does not end the lead") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    static_cast<void>(wolf.step(sensing(100, 105, 5)));
    auto lost = calm();
    for (int i = 0; i < 20; ++i) CHECK(wolf.step(lost).move == CompanionMove::Lead);
    CHECK(wolf.step(sensing(99, 104, 5)).move == CompanionMove::Lead);
    for (int i = 0; i < 32; ++i) static_cast<void>(wolf.step(lost));
    CHECK(wolf.mode() == CompanionMode::Follow);
}

TEST_CASE("Food, fights and a Come order outrank the lead") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    static_cast<void>(wolf.step(sensing(100, 105, 5)));
    auto input = sensing(99, 104, 5);
    input.target = {true, false, 10, 0, 0};
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Notice);

    Companion called;
    called.arrive(false);
    settle(called);
    static_cast<void>(called.step(sensing(100, 105, 5)));
    input = sensing(99, 104, 5);
    input.come = true;
    CHECK(called.step(input).move == CompanionMove::Follow);
    input.come = false;
    CHECK(called.step(input).move == CompanionMove::Follow);
}

TEST_CASE("Beyond 75 units or in deep water the wolf warps") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.playerDistance = 76;
    CHECK(plays(wolf, input, "WarpOut"));
    CompanionOutput output;
    for (int i = 0; i < 3 && !output.warp; ++i) output = wolf.step(input);
    CHECK(output.warp);
    CHECK(wolf.mode() == CompanionMode::WarpWait);
    input.warpFinished = true;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::WarpIn);

    Companion swimmer;
    swimmer.arrive(false);
    settle(swimmer);
    auto water = calm();
    water.deepWater = true;
    CHECK(plays(swimmer, water, "WarpOut"));
}

TEST_CASE("A failed warp keeps the wolf hidden and asks again a second later") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.playerDistance = 90;
    for (int i = 0; i < 4; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::WarpWait);
    input.warpFailed = true;
    CHECK_FALSE(wolf.step(input).warp);
    input.warpFailed = false;
    int waited = 0;
    while (!wolf.step(input).warp && waited < 200) ++waited;
    CHECK(wolf.mode() == CompanionMode::WarpWait);
    CHECK(waited >= 58);
    CHECK(waited <= 61);
    input.warpFinished = true;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::WarpIn);
}

TEST_CASE("Putting the wolf away plays the fade-out and then lets the actor go") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.dismiss = true;
    input.animationDone = false;
    const auto first = wolf.step(input);
    CHECK(wolf.mode() == CompanionMode::WarpOut);
    CHECK(std::string_view(first.play) == "WarpOut");
    CHECK_FALSE(first.leave);
    input.dismiss = false;
    CHECK_FALSE(wolf.step(input).leave);
    input.animationDone = true;
    const auto done = wolf.step(input);
    CHECK(done.leave);
    CHECK_FALSE(done.warp);
    CHECK(wolf.mode() == CompanionMode::Gone);

    Companion hidden;
    hidden.arrive(false);
    settle(hidden);
    auto far = calm();
    far.playerDistance = 90;
    for (int i = 0; i < 4; ++i) static_cast<void>(hidden.step(far));
    REQUIRE(hidden.mode() == CompanionMode::WarpWait);
    far.dismiss = true;
    CHECK(hidden.step(far).leave);
    CHECK(hidden.mode() == CompanionMode::Gone);
}

TEST_CASE("The materialize flash fires as the wolf fades in and out") {
    Companion wolf;
    wolf.arrive(false);
    auto input = calm();
    input.animationDone = false;
    auto output = wolf.step(input);
    CHECK(std::string_view(output.play) == "Spawn");
    CHECK(output.appear);
    CHECK_FALSE(output.vanish);
    input.animationDone = true;
    output = wolf.step(input);
    CHECK(std::string_view(output.play) == "Howling");
    CHECK_FALSE(output.appear);
    settle(wolf);
    input.playerDistance = 90;
    input.animationDone = false;
    output = {};
    for (int i = 0; i < 3 && !output.play; ++i) output = wolf.step(input);
    REQUIRE(output.play);
    CHECK(std::string_view(output.play) == "WarpOut");
    CHECK(output.vanish);
    CHECK_FALSE(output.appear);
    input.animationDone = true;
    for (int i = 0; i < 3 && wolf.mode() != CompanionMode::WarpWait; ++i) static_cast<void>(wolf.step(input));
    input.warpFinished = true;
    for (int i = 0; i < 3 && wolf.mode() != CompanionMode::WarpIn; ++i) static_cast<void>(wolf.step(input));
    REQUIRE(wolf.mode() == CompanionMode::WarpIn);
    input.warpFinished = false;
    input.playerDistance = 3;
    input.animationDone = false;
    output = wolf.step(input);
    CHECK(std::string_view(output.play) == "WarpIn");
    CHECK(output.appear);
    CHECK_FALSE(output.vanish);
}

namespace {
// Two route failures in a row push the wolf from Follow through FollowRetry into a warp. Beyond the
// leash break this is a plain catch-up; within it the wolf hides for a wait instead.
void failFollow(Companion& wolf, CompanionInput input, float distance = 35) {
    input.playerDistance = distance;
    input.routeFailed = true;
    for (int i = 0; i < 8 && wolf.mode() != CompanionMode::WarpWait; ++i) static_cast<void>(wolf.step(input));
    REQUIRE(wolf.mode() == CompanionMode::WarpWait);
}
// The wait input keeps the failure's distance so the player has not "moved elsewhere".
CompanionInput waitingAt(float distance = 35) {
    auto input = calm();
    input.playerDistance = distance;
    return input;
}
int ticksUntilWarp(Companion& wolf, const CompanionInput& input, int limit) {
    for (int waited = 0; waited < limit; ++waited)
        if (wolf.step(input).warp) return waited;
    return limit;
}
void finishWarp(Companion& wolf) {
    auto input = calm();
    input.warpFinished = true;
    static_cast<void>(wolf.step(input));
    REQUIRE(wolf.mode() == CompanionMode::WarpIn);
    settle(wolf);
    REQUIRE(wolf.mode() == CompanionMode::Follow);
}
}

TEST_CASE("Quick repeated follow-failure warps escalate into a hidden wait") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    failFollow(wolf, calm());
    CHECK(ticksUntilWarp(wolf, waitingAt(), 5) < 5);
    finishWarp(wolf);
    failFollow(wolf, calm());
    const int second = ticksUntilWarp(wolf, waitingAt(), 200);
    CHECK(second >= 85);
    CHECK(second <= 93);
    finishWarp(wolf);
    failFollow(wolf, calm());
    const int third = ticksUntilWarp(wolf, waitingAt(), 400);
    CHECK(third >= 235);
    CHECK(third <= 243);
}

TEST_CASE("Close-range route failures hide the wolf for a wait instead of warping onto the player") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    failFollow(wolf, calm(), 20);
    const int held = ticksUntilWarp(wolf, waitingAt(20), 200);
    CHECK(held >= 85);
    CHECK(held <= 93);
}

TEST_CASE("A hidden wait ends early when the player moves elsewhere") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    failFollow(wolf, calm(), 20);
    for (int i = 0; i < 10; ++i) CHECK_FALSE(wolf.step(waitingAt(20)).warp);
    CHECK(wolf.step(waitingAt(33)).warp);
}

TEST_CASE("A whistle or a come order ends the hidden wait at once") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    failFollow(wolf, calm());
    static_cast<void>(ticksUntilWarp(wolf, waitingAt(), 5));
    finishWarp(wolf);
    failFollow(wolf, calm());
    auto input = waitingAt();
    for (int i = 0; i < 10; ++i) CHECK_FALSE(wolf.step(input).warp);
    input.whistle = true;
    CHECK(wolf.step(input).warp);
}

TEST_CASE("Healthy following resets the warp escalation and distance warps stay immediate") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    failFollow(wolf, calm());
    static_cast<void>(ticksUntilWarp(wolf, waitingAt(), 5));
    finishWarp(wolf);
    auto steady = calm();
    steady.playerDistance = 30;
    for (int i = 0; i < 310; ++i) static_cast<void>(wolf.step(steady));
    REQUIRE(wolf.mode() == CompanionMode::Follow);
    failFollow(wolf, calm());
    CHECK(ticksUntilWarp(wolf, waitingAt(), 5) < 5);
    finishWarp(wolf);
    auto far = calm();
    far.playerDistance = 90;
    far.animationDone = true;
    for (int i = 0; i < 3 && wolf.mode() != CompanionMode::WarpWait; ++i) static_cast<void>(wolf.step(far));
    REQUIRE(wolf.mode() == CompanionMode::WarpWait);
    CHECK(ticksUntilWarp(wolf, far, 5) < 5);
}

TEST_CASE("A hostile within 45 units gets the notice turn and roar before battle") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    static_cast<void>(wolf.step(enemy(40)));
    CHECK(wolf.mode() == CompanionMode::Notice);
    CHECK(wolf.step(enemy(40)).aim == CompanionAim::Target);
    Companion far;
    far.arrive(false);
    settle(far);
    static_cast<void>(far.step(enemy(46)));
    CHECK(far.mode() == CompanionMode::Follow);
}

TEST_CASE("An attack that reaches deals power and builds charge") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    int damage = 0;
    for (int i = 0; i < 200 && wolf.charge() == 0; ++i) damage += wolf.step(enemy(3)).damage;
    CHECK(damage + kNativeBitePower == companionPower(kMaxLife, false));
    CHECK(wolf.charge() >= 7);
    CHECK(wolf.charge() < 10);
}

TEST_CASE("Charge reaching full starts a five-attack chain that ends with a howl") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    int damage = 0;
    bool chained = false, howled = false;
    for (int i = 0; i < 6000 && !howled; ++i) {
        const auto output = wolf.step(enemy(3));
        damage += output.damage;
        chained |= output.play && std::string_view(output.play) == "Attack_ChainAttack";
        howled = chained && output.play && std::string_view(output.play) == "Howling";
    }
    CHECK(chained);
    CHECK(howled);
    CHECK(wolf.charge() == 0.0f);
}

TEST_CASE("A chain requires full life") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto hurt = calm();
    hurt.hit = 7;
    static_cast<void>(wolf.step(hurt));
    settle(wolf, [&] { auto i = calm(); i.life = wolf.life(); return i; }());
    bool chained = false;
    for (int i = 0; i < 3000; ++i) {
        auto input = enemy(3);
        input.life = wolf.life();
        const auto output = wolf.step(input);
        chained |= output.play && std::string_view(output.play) == "Attack_ChainAttack";
    }
    CHECK_FALSE(chained);
    CHECK(wolf.charge() == 100.0f);
}

TEST_CASE("Damage is reduced by defense and answered with one reaction") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.hit = 7;
    const auto output = wolf.step(input);
    CHECK(wolf.life() == kMaxLife - 1);
    CHECK(output.setLife == kMaxLife - 1);
    CHECK(std::string_view(output.play) == "Damage");
    input.hit = 30;
    input.life = kMaxLife - 1;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.life() == kMaxLife - 1);

    Companion heavy;
    heavy.arrive(false);
    settle(heavy);
    input.life = kMaxLife;
    CHECK(std::string_view(heavy.step(input).play) == "Dead");
    CHECK(heavy.life() == kMaxLife - 15);
}

TEST_CASE("At twenty hearts the wolf falls on the sixth 30-point swing") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    int swings = 0;
    for (int i = 0; i < 4000 && !wolf.dead(); ++i) {
        auto input = calm();
        input.life = wolf.life();
        input.hit = i % 200 == 0 ? 30 : 0;
        swings += input.hit ? 1 : 0;
        static_cast<void>(wolf.step(input));
    }
    CHECK(wolf.dead());
    CHECK(swings == 6);
}

TEST_CASE("Death plays Dead, Standup and WarpOut and then retires") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    bool retired = false;
    for (int i = 0; i < 4000 && !retired; ++i) {
        auto input = calm();
        input.hit = i % 150 == 0 ? kMaxLife : 0;
        retired = wolf.step(input).retire;
    }
    CHECK(retired);
    CHECK(wolf.life() == 0);
}

TEST_CASE("Food in range is eaten, dropped food grants the power-up with a howl") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.food = {true, true, 1, 0, 4};
    bool ate = false, howled = false;
    for (int i = 0; i < 12; ++i) {
        const auto output = wolf.step(input);
        ate |= output.eat;
        howled |= output.play && std::string_view(output.play) == "Howling";
        if (ate) input.food.present = false;
    }
    CHECK(ate);
    CHECK(howled);
    CHECK(wolf.fed());
    CHECK(companionPower(kMaxLife, wolf.fed()) == 75);
}

TEST_CASE("Food that cannot be reached is given up after the original timeout") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.food = {true, true, 20, 0, 4};
    bool rejected = false;
    for (int i = 0; i < 310 && !rejected; ++i) rejected = wolf.step(input).rejectFood;
    CHECK(rejected);
}

TEST_CASE("Sprinting away ends the notice turn at once") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    static_cast<void>(wolf.step(enemy(40)));
    REQUIRE(wolf.mode() == CompanionMode::Notice);
    auto input = enemy(40);
    input.leaderLeaving = input.leaderFleeing = true;
    CHECK(wolf.step(input).move == CompanionMove::Follow);
    CHECK(wolf.mode() == CompanionMode::Follow);
}

TEST_CASE("Past the leash the notice yields to following") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    static_cast<void>(wolf.step(enemy(40)));
    REQUIRE(wolf.mode() == CompanionMode::Notice);
    auto input = enemy(40);
    input.playerDistance = 35;
    CHECK(wolf.step(input).move == CompanionMove::Follow);
    CHECK(wolf.mode() == CompanionMode::Follow);
}

TEST_CASE("A far player ends the look-around with a warp") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    for (int i = 0; i < 12 && wolf.mode() != CompanionMode::Battle; ++i) static_cast<void>(wolf.step(enemy(40)));
    REQUIRE(wolf.mode() == CompanionMode::Battle);
    static_cast<void>(wolf.step(calm()));
    REQUIRE(wolf.mode() == CompanionMode::Lookout);
    auto input = calm();
    input.playerDistance = 50;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::WarpOut);
}

TEST_CASE("Sprinting away pulls the wolf out of a fight at once") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    for (int i = 0; i < 12 && wolf.mode() != CompanionMode::Battle; ++i) static_cast<void>(wolf.step(enemy(40)));
    REQUIRE(wolf.mode() == CompanionMode::Battle);
    auto input = enemy(40);
    input.leaderLeaving = input.leaderFleeing = true;
    const auto output = wolf.step(input);
    CHECK(output.move == CompanionMove::Follow);
    CHECK(output.rejectTarget == 300.0f);
    REQUIRE(output.gaveUp);
    CHECK(std::string_view(output.gaveUp) == "flee");
}

TEST_CASE("Sprinting away pulls the wolf off a meal and blocks every new distraction") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.food = {true, true, 6, 0, 4};
    for (int i = 0; i < 4 && wolf.mode() != CompanionMode::ToFood; ++i) static_cast<void>(wolf.step(input));
    REQUIRE(wolf.mode() == CompanionMode::ToFood);
    input.leaderLeaving = input.leaderFleeing = true;
    const auto output = wolf.step(input);
    CHECK(output.rejectFood);
    CHECK(output.move == CompanionMove::Follow);
    auto busy = calm();
    busy.leaderLeaving = busy.leaderFleeing = true;
    busy.target = {true, false, 10, 0, 0};
    busy.food = {true, true, 6, 0, 4};
    for (int i = 0; i < 5; ++i) CHECK(wolf.step(busy).move == CompanionMove::Follow);
}

TEST_CASE("Walking away forgoes new prey hunts and scavenging but not enemies") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto hurt = calm();
    hurt.hit = 1;
    static_cast<void>(wolf.step(hurt));
    settle(wolf);
    REQUIRE(wolf.mode() == CompanionMode::Follow);
    REQUIRE(wolf.life() < kMaxLife);
    auto leaving = calm();
    leaving.leaderLeaving = true;
    leaving.target = {true, true, 20, 0, 0};
    leaving.food = {true, false, 6, 0, 4};
    leaving.life = wolf.life();
    for (int i = 0; i < 5; ++i) CHECK(wolf.step(leaving).move == CompanionMove::Follow);
    auto stopped = leaving;
    stopped.leaderLeaving = false;
    stopped.target = {};
    static_cast<void>(wolf.step(stopped));
    CHECK(wolf.mode() == CompanionMode::Notice);

    Companion guard;
    guard.arrive(false);
    settle(guard);
    auto threatened = enemy(20);
    threatened.leaderLeaving = true;
    static_cast<void>(guard.step(threatened));
    CHECK(guard.mode() == CompanionMode::Notice);
}

TEST_CASE("One meal per stop: a cooldown follows each bite") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.food = {true, true, 1, 0, 4};
    bool ate = false;
    for (int i = 0; i < 12; ++i) {
        ate |= wolf.step(input).eat;
        if (ate) input.food.present = false;
    }
    REQUIRE(ate);
    settle(wolf);
    auto again = calm();
    again.food = {true, true, 6, 0, 4};
    for (int i = 0; i < 20; ++i) CHECK(wolf.step(again).move == CompanionMove::Follow);
    for (int i = 0; i < 900; ++i) static_cast<void>(wolf.step(calm()));
    static_cast<void>(wolf.step(again));
    CHECK(wolf.mode() == CompanionMode::Notice);
}

TEST_CASE("Scavenged food is only taken below full health") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.food = {true, false, 6, 0, 4};
    for (int i = 0; i < 5; ++i) CHECK(wolf.step(input).move == CompanionMove::Follow);
    auto hurt = calm();
    hurt.hit = 1;
    static_cast<void>(wolf.step(hurt));
    settle(wolf);
    REQUIRE(wolf.life() < kMaxLife);
    input.life = wolf.life();
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Notice);
}

TEST_CASE("A target that stays too far above is restricted") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = enemy(30);
    input.target.height = 9;
    CompanionOutput output;
    for (int i = 0; i < 80 && output.rejectTarget == 0; ++i) output = wolf.step(input);
    CHECK(output.rejectTarget == 300.0f);
    REQUIRE(output.gaveUp);
    CHECK(std::string_view(output.gaveUp) == "height");
}

namespace {
// Brings a settled wolf through the notice and the battle pause into the rush at the enemy.
void closeIn(Companion& wolf, const CompanionInput& input) {
    for (int i = 0; i < 200 && wolf.mode() != CompanionMode::Close; ++i) static_cast<void>(wolf.step(input));
}
}

TEST_CASE("In reach but facing away, the wolf turns with Turn_Attack and then bites, aimed") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = enemy(3);
    input.target.angle = 3;
    bool turned = false, bit = false;
    for (int i = 0; i < 200 && !bit; ++i) {
        const auto output = wolf.step(input);
        const std::string_view played = output.play ? output.play : "";
        if (played == "Turn_Attack" && wolf.mode() == CompanionMode::AttackTurn) {
            turned = true;
            input.target.angle = 0;  // The turn left him facing the enemy.
        }
        bit = turned && played == "Attack" && output.aim == CompanionAim::Target;
    }
    CHECK(turned);
    CHECK(bit);
}

TEST_CASE("A turn that leaves the wolf facing away is followed by another turn, never a bite") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = enemy(3);
    input.target.angle = 3;
    int turns = 0;
    bool bit = false;
    const char* gaveUp = nullptr;
    for (int i = 0; i < 400 && !gaveUp; ++i) {
        const auto output = wolf.step(input);
        const std::string_view played = output.play ? output.play : "";
        turns += played == "Turn_Attack" && wolf.mode() == CompanionMode::AttackTurn;
        bit |= played == "Attack";
        gaveUp = output.gaveUp;
    }
    CHECK(turns >= 2);
    CHECK_FALSE(bit);
    REQUIRE(gaveUp);
    CHECK(std::string_view(gaveUp) == "time");
}

TEST_CASE("Aimed turns are steered every tick they play, the roar and the bite are not") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = enemy(20);
    input.animationDone = false;
    CompanionOutput output;
    for (int i = 0; i < 3 && !output.play; ++i) output = wolf.step(input);
    REQUIRE(output.play);
    REQUIRE(std::string_view(output.play) == "Turn_Attack");
    for (int i = 0; i < 5; ++i) {
        output = wolf.step(input);
        CHECK(output.steer);
        CHECK(output.aim == CompanionAim::Target);
    }
    input.animationDone = true;
    output = wolf.step(input);
    REQUIRE(output.play);
    CHECK(std::string_view(output.play) == "Roar");
    CHECK_FALSE(output.steer);

    Companion biter;
    biter.arrive(false);
    settle(biter);
    auto close = enemy(3);
    close.target.angle = 3;
    bool steered = false, bitSteered = false;
    for (int i = 0; i < 200; ++i) {
        const auto step = biter.step(close);
        const std::string_view played = step.play ? step.play : "";
        if (biter.mode() == CompanionMode::AttackTurn) {
            steered |= step.steer;
            close.target.angle = 0;
        }
        if (played == "Attack") {
            bitSteered = step.steer;
            break;
        }
    }
    CHECK(steered);
    CHECK_FALSE(bitSteered);
}

TEST_CASE("The chain's pre-bite turn plays at double rate and is not steered") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = enemy(3);
    bool chained = false, turned = false;
    for (int i = 0; i < 6000 && !turned; ++i) {
        const auto output = wolf.step(input);
        const std::string_view played = output.play ? output.play : "";
        if (played == "Attack_ChainAttack") {
            chained = true;
            input.target.angle = 3;
        }
        if (chained && played == "Turn_Attack" && wolf.mode() == CompanionMode::AttackTurn) {
            turned = true;
            CHECK(output.rate == 2.0f);
            CHECK_FALSE(output.steer);
        }
    }
    CHECK(chained);
    CHECK(turned);
}

namespace {
// One step at the given distance from the enemy, as the band move it produced.
std::pair<CompanionMove, CircleBand> bandAt(Companion& wolf, CompanionInput& input, float distance) {
    input.target.distance = distance;
    const auto output = wolf.step(input);
    return {output.move, output.circle};
}
}

TEST_CASE("Between attacks the wolf closes in, then circles the enemy, and runs at it again beyond the band") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = enemy(30);
    for (int i = 0; i < 40 && wolf.mode() != CompanionMode::Battle; ++i) static_cast<void>(wolf.step(input));
    REQUIRE(wolf.mode() == CompanionMode::Battle);
    CHECK(bandAt(wolf, input, 20).first == CompanionMove::Chase);
    CHECK(bandAt(wolf, input, 9).first == CompanionMove::Chase);  // The original closes until inside 9.
    CHECK(bandAt(wolf, input, 8.5f) == std::pair{CompanionMove::Circle, CircleBand::Inner});
    CHECK(bandAt(wolf, input, 11.5f) == std::pair{CompanionMove::Circle, CircleBand::Inner});
    CHECK(bandAt(wolf, input, 14) == std::pair{CompanionMove::Circle, CircleBand::Outer});
    CHECK(bandAt(wolf, input, 10.5f) == std::pair{CompanionMove::Circle, CircleBand::Outer});
    CHECK(bandAt(wolf, input, 9.5f) == std::pair{CompanionMove::Circle, CircleBand::Inner});
    CHECK(bandAt(wolf, input, 16).first == CompanionMove::Chase);
}

TEST_CASE("Too close between attacks, the wolf backs off to the wide circle until beyond the leave distance") {
    for (const bool prey : {false, true}) {
        Companion wolf;
        wolf.arrive(false);
        settle(wolf);
        auto input = enemy(30);
        input.target.prey = prey;
        for (int i = 0; i < 40 && wolf.mode() != CompanionMode::Battle; ++i) static_cast<void>(wolf.step(input));
        REQUIRE(wolf.mode() == CompanionMode::Battle);
        // The hunting band is 10 / 5 / 1 / 6; the battle band 15 / 9 / 4 / 11.
        const float leaveStart = prey ? 1.0f : 4.0f, leaveEnd = prey ? 6.0f : 11.0f;
        CHECK(bandAt(wolf, input, leaveStart + 1).first == CompanionMove::Circle);
        CHECK(bandAt(wolf, input, leaveStart - 0.5f) == std::pair{CompanionMove::Circle, CircleBand::Leave});
        CHECK(bandAt(wolf, input, leaveEnd - 0.5f) == std::pair{CompanionMove::Circle, CircleBand::Leave});
        const auto back = bandAt(wolf, input, leaveEnd + 0.5f);
        CHECK(back.first == CompanionMove::Circle);
        CHECK(back.second != CircleBand::Leave);
    }
}

TEST_CASE("Out of attack range the wolf turns and barks at the original's flat chance; never at prey") {
    for (const bool prey : {false, true}) {
        Companion wolf(7);
        wolf.arrive(false);
        settle(wolf);
        auto input = enemy(prey ? 45.0f : 30.0f);  // At the hunt's 45, prey is still in sight but out of attack range.
        input.target.prey = prey;
        for (int i = 0; i < 40 && wolf.mode() != CompanionMode::Battle; ++i) static_cast<void>(wolf.step(input));
        REQUIRE(wolf.mode() == CompanionMode::Battle);
        int barks = 0, turns = 0;
        for (int i = 0; i < 60000; ++i) {
            const auto output = wolf.step(input);
            const std::string_view played = output.play ? output.play : "";
            barks += played == "Roar";
            turns += played == "Turn_Attack";
        }
        if (prey) {
            CHECK(barks == 0);
        } else {
            // One attack check every 30 to 60 ticks: about 1330 checks, so about 73 barks at 5.5 percent.
            CHECK(barks > 45);
            CHECK(barks < 110);
            CHECK(turns == barks);
        }
    }
}

TEST_CASE("Back in a fight soon after the last, the wolf skips the notice turn and roar") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = enemy(10);
    for (int i = 0; i < 40 && wolf.mode() != CompanionMode::Battle; ++i) static_cast<void>(wolf.step(input));
    REQUIRE(wolf.mode() == CompanionMode::Battle);
    auto fleeing = input;
    fleeing.leaderFleeing = true;
    static_cast<void>(wolf.step(fleeing));
    REQUIRE(wolf.mode() == CompanionMode::Follow);
    for (int i = 0; i < 60; ++i) static_cast<void>(wolf.step(calm()));
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Battle);
    for (int i = 0; i < 300; ++i) static_cast<void>(wolf.step(calm()));
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Notice);
}

TEST_CASE("A sprint away counts only once it lasts, and ends after a calm spell or once the player is close") {
    FleeLatch flee;
    for (int i = 0; i < 9; ++i) CHECK_FALSE(flee.update(true, 8, 1));
    CHECK(flee.update(true, 8, 1));
    for (int i = 0; i < 44; ++i) CHECK(flee.update(false, 8, 1));
    CHECK_FALSE(flee.update(false, 8, 1));
    for (int i = 0; i < 10; ++i) static_cast<void>(flee.update(true, 8, 1));
    CHECK(flee.update(false, 8, 1));
    CHECK_FALSE(flee.update(false, 4, 1));
}

TEST_CASE("A refused route holds the wolf for the original's cooldown, and too much waiting drops the enemy") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = enemy(10);
    closeIn(wolf, input);
    REQUIRE(wolf.mode() == CompanionMode::Close);
    input.routeFailed = true;
    CHECK(wolf.step(input).move == CompanionMove::Hold);
    input.routeFailed = false;
    int held = 1;
    while (held < 300 && wolf.step(input).move == CompanionMove::Hold) ++held;
    CHECK(held == 120);
    CHECK(wolf.mode() == CompanionMode::Close);
    input.routeFailed = true;
    auto output = wolf.step(input);
    input.routeFailed = false;
    int waited = 1;
    while (waited < 300 && !output.gaveUp) {
        output = wolf.step(input);
        ++waited;
    }
    REQUIRE(output.gaveUp);
    CHECK(std::string_view(output.gaveUp) == "route");
    CHECK(waited == 100);
    CHECK(output.rejectTarget == 300.0f);
    CHECK(wolf.mode() == CompanionMode::Battle);
}

TEST_CASE("Going round an obstacle is not a wall: only standing without gaining counts") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = enemy(10);
    closeIn(wolf, input);
    REQUIRE(wolf.mode() == CompanionMode::Close);
    for (int i = 0; i < 100; ++i) REQUIRE(wolf.step(input).move == CompanionMove::Chase);
    input.standing = true;
    const char* reason = nullptr;
    for (int i = 0; i < 60 && !reason; ++i) reason = wolf.step(input).gaveUp;
    REQUIRE(reason);
    CHECK(std::string_view(reason) == "wall");

    Companion runner;
    runner.arrive(false);
    settle(runner);
    auto moving = enemy(10);
    closeIn(runner, moving);
    REQUIRE(runner.mode() == CompanionMode::Close);
    reason = nullptr;
    int ticks = 0;
    while (ticks < 400 && !reason) {
        reason = runner.step(moving).gaveUp;
        ++ticks;
    }
    REQUIRE(reason);
    CHECK(std::string_view(reason) == "time");
    CHECK(ticks == 180);
}

TEST_CASE("Life is protected while warping") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.playerDistance = 90;
    input.animationDone = false;
    static_cast<void>(wolf.step(input));
    input.hit = 8;
    input.life = 4;
    CHECK(wolf.step(input).setLife == kMaxLife);
    CHECK(wolf.life() == kMaxLife);
}

TEST_CASE("Told to stay, the wolf holds still and ignores fights and food") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.stay = true;
    CHECK(wolf.step(input).move == CompanionMove::Hold);
    CHECK(wolf.mode() == CompanionMode::WaitOrder);
    CHECK(wolf.ordered());
    auto busy = enemy(4);
    busy.food = {true, false, 2, 0, 4};
    for (int i = 0; i < 40; ++i) CHECK(wolf.step(busy).move == CompanionMove::Hold);
    CHECK(wolf.mode() == CompanionMode::WaitOrder);
}

TEST_CASE("Staying re-faces the player only past 100 degrees and not when the player is close") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.stay = true;
    static_cast<void>(wolf.step(input));
    auto beside = calm();
    beside.leaderAngle = 1.7f;
    beside.playerDistance = 6;
    static_cast<void>(wolf.step(beside));
    CHECK(wolf.mode() == CompanionMode::WaitOrder);
    auto close = calm();
    close.leaderAngle = 3;
    close.playerDistance = 2;
    static_cast<void>(wolf.step(close));
    CHECK(wolf.mode() == CompanionMode::WaitOrder);
    auto behind = calm();
    behind.leaderAngle = 3;
    behind.playerDistance = 6;
    behind.animationDone = false;
    static_cast<void>(wolf.step(behind));
    CHECK(wolf.mode() == CompanionMode::Facing);
    const auto turn = wolf.step(behind);
    CHECK(std::string_view(turn.play) == "Turn_Notice_NoGrowl");
    CHECK(turn.aim == CompanionAim::Leader);
    behind.animationDone = true;
    static_cast<void>(wolf.step(behind));
    CHECK(wolf.mode() == CompanionMode::WaitOrder);
}

TEST_CASE("Come releases a staying wolf at once, without a notice turn") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.stay = true;
    static_cast<void>(wolf.step(input));
    input = calm();
    input.come = true;
    CHECK(wolf.step(input).move == CompanionMove::Follow);
    CHECK(wolf.mode() == CompanionMode::Follow);
    CHECK_FALSE(wolf.ordered());
}

TEST_CASE("A whistle waits for the sound to arrive, then turns the wolf and brings it back") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.stay = true;
    static_cast<void>(wolf.step(input));
    input = calm();
    input.whistle = true;
    input.playerDistance = 22;
    static_cast<void>(wolf.step(input));
    auto quiet = calm();
    quiet.playerDistance = 22;
    for (int i = 0; i < 11; ++i) {
        static_cast<void>(wolf.step(quiet));
        CHECK(wolf.mode() == CompanionMode::WaitOrder);
    }
    static_cast<void>(wolf.step(quiet));
    CHECK(wolf.mode() == CompanionMode::Notice);
    CHECK_FALSE(wolf.ordered());
    const auto turn = wolf.step(quiet);
    CHECK(turn.aim == CompanionAim::Leader);
    CHECK(wolf.step(quiet).move == CompanionMove::Follow);
    CHECK(wolf.mode() == CompanionMode::Follow);
}

TEST_CASE("A whistle pulls the wolf out of a fight") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    settle(wolf, enemy(30));
    REQUIRE(wolf.mode() != CompanionMode::Follow);
    auto input = enemy(30);
    input.whistle = true;
    input.playerDistance = 0;
    static_cast<void>(wolf.step(input));
    for (int i = 0; i < 11 && wolf.mode() != CompanionMode::Notice; ++i) static_cast<void>(wolf.step(enemy(30)));
    CHECK(wolf.mode() == CompanionMode::Notice);
}

TEST_CASE("Orders given while the wolf cannot listen are thrown away") {
    Companion wolf;
    wolf.arrive(false);
    CHECK_FALSE(wolf.listening());
    auto input = calm();
    input.stay = true;
    input.whistle = true;
    static_cast<void>(wolf.step(input));
    settle(wolf);
    CHECK(wolf.mode() == CompanionMode::Follow);
    CHECK_FALSE(wolf.ordered());
}

TEST_CASE("Staying wolves still warp when the player leaves, and the order ends") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.stay = true;
    static_cast<void>(wolf.step(input));
    auto far = calm();
    far.playerDistance = 80;
    far.leaderAngle = 2;
    far.animationDone = false;
    static_cast<void>(wolf.step(far));
    CHECK(wolf.mode() == CompanionMode::WarpOut);
    CHECK_FALSE(wolf.ordered());
    const auto turn = wolf.step(far);
    CHECK(std::string_view(turn.play) == "Turn_Notice_NoGrowl");
    CHECK(turn.aim == CompanionAim::Leader);
    far.animationDone = true;
    CHECK(std::string_view(wolf.step(far).play) == "WarpOut");
}

TEST_CASE("Warping out of deep water skips the turn") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.deepWater = true;
    input.leaderAngle = 3;
    static_cast<void>(wolf.step(input));
    CHECK(plays(wolf, input, "WarpOut"));
}

TEST_CASE("A hit ends a stay order") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.stay = true;
    static_cast<void>(wolf.step(input));
    auto hit = calm();
    hit.hit = 7;
    hit.animationDone = false;
    static_cast<void>(wolf.step(hit));
    CHECK(wolf.mode() == CompanionMode::Reaction);
    auto after = calm();
    after.life = wolf.life();
    settle(wolf, after);
    CHECK(wolf.mode() == CompanionMode::Follow);
    CHECK_FALSE(wolf.ordered());
}

TEST_CASE("Food gets a notice turn toward it before the wolf walks over") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.food = {true, true, 6, 0, 4};
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Notice);
    CHECK(wolf.step(input).aim == CompanionAim::Food);
    CHECK(wolf.step(input).move == CompanionMove::ToFood);
    CHECK(wolf.mode() == CompanionMode::ToFood);
}

TEST_CASE("A lost target gets the look-around before the wolf returns") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    settle(wolf, enemy(30));
    REQUIRE(wolf.mode() == CompanionMode::Battle);
    auto gone = calm();
    gone.animationDone = false;
    static_cast<void>(wolf.step(gone));
    CHECK(wolf.mode() == CompanionMode::Lookout);
    CHECK(std::string_view(wolf.step(gone).play) == "Look");
    settle(wolf);
    CHECK(wolf.mode() == CompanionMode::Follow);
}

TEST_CASE("A wolf at rest turns back to the player past 100 degrees") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.standing = true;
    input.leaderAngle = 2;
    input.playerDistance = 5;
    input.animationDone = false;
    for (int i = 0; i < 9; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Facing);
    CHECK(std::string_view(wolf.step(input).play) == "Turn_Notice_NoGrowl");
}

TEST_CASE("A cutscene stops the wolf where it is and it follows again afterwards") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.event = true;
    const auto output = wolf.step(input);
    CHECK(wolf.mode() == CompanionMode::EventHold);
    CHECK(output.move == CompanionMove::Hold);
    CHECK_FALSE(wolf.listening());
    input.event = false;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
}

TEST_CASE("A cutscene ignores fights and orders, and a stay order survives it") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.stay = true;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::WaitOrder);
    input = enemy(4);
    input.event = true;
    input.come = true;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::EventHold);
    input.come = false;
    settle(wolf, input);
    CHECK(wolf.mode() == CompanionMode::EventHold);
    static_cast<void>(wolf.step(calm()));
    CHECK(wolf.mode() == CompanionMode::WaitOrder);
    CHECK(wolf.ordered());
}

TEST_CASE("A long cutscene sends the wolf away and it returns only when the cutscene ends") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.event = true;
    bool warped = false;
    for (int i = 0; i < 299; ++i) warped |= wolf.step(input).warp;
    CHECK(wolf.mode() == CompanionMode::EventHold);
    for (int i = 0; i < 12; ++i) warped |= wolf.step(input).warp;
    CHECK(wolf.mode() == CompanionMode::WarpWait);
    CHECK_FALSE(warped);
    input.event = false;
    CHECK(wolf.step(input).warp);
    CHECK_FALSE(wolf.step(input).warp);
    input.warpFinished = true;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::WarpIn);
}

TEST_CASE("Every new cutscene gets the full wait") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.event = true;
    for (int i = 0; i < 200; ++i) static_cast<void>(wolf.step(input));
    settle(wolf);
    for (int i = 0; i < 200; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::EventHold);
}

TEST_CASE("With no ground near the player the wolf waits instead of following") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.noPath = true;
    const auto output = wolf.step(input);
    CHECK(wolf.mode() == CompanionMode::NoPath);
    CHECK(output.move == CompanionMove::Hold);
    CHECK_FALSE(wolf.listening());
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::NoPath);
    input.noPath = false;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
}

TEST_CASE("The no-path wait still gives way to a fight and to the warp distance") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = enemy(20);
    input.noPath = true;
    auto quiet = calm();
    quiet.noPath = true;
    static_cast<void>(wolf.step(quiet));
    REQUIRE(wolf.mode() == CompanionMode::NoPath);
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Notice);

    Companion far;
    far.arrive(false);
    settle(far);
    static_cast<void>(far.step(quiet));
    REQUIRE(far.mode() == CompanionMode::NoPath);
    quiet.playerDistance = 80;
    static_cast<void>(far.step(quiet));
    CHECK(far.mode() == CompanionMode::WarpOut);
}

TEST_CASE("A planned route that fails counts straight away as a lost follow") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.playerDistance = 30;
    input.routeFailed = true;
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::FollowRetry);
}

TEST_CASE("A failed route ends a lead only when the target is still far away") {
    Companion near;
    near.arrive(false);
    settle(near);
    auto input = calm();
    input.lead = {true, 50, 60};
    static_cast<void>(near.step(input));
    REQUIRE(near.mode() == CompanionMode::Lead);
    input.routeFailed = true;
    static_cast<void>(near.step(input));
    CHECK(near.mode() == CompanionMode::Lead);

    Companion far;
    far.arrive(false);
    settle(far);
    input = calm();
    input.lead = {true, 90, 100};
    static_cast<void>(far.step(input));
    REQUIRE(far.mode() == CompanionMode::Lead);
    input.routeFailed = true;
    static_cast<void>(far.step(input));
    CHECK(far.mode() != CompanionMode::Lead);
}

TEST_CASE("A wolf stuck far from the player retries once and then warps") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.playerDistance = 30;
    input.standing = true;
    input.leaderAngle = 0;
    input.animationDone = false;
    for (int i = 0; i < 89; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::FollowRetry);
    CHECK_FALSE(wolf.listening());
    for (int i = 0; i < 40 && wolf.mode() != CompanionMode::Follow; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
    for (int i = 0; i < 85; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
    for (int i = 0; i < 6; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::WarpOut);
}

TEST_CASE("A wolf standing short of a standing player retries, then warps; only a moving player excuses it") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.playerDistance = 10;
    input.standing = true;
    input.leaderAngle = 0;
    input.animationDone = false;
    for (int i = 0; i < 300; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
    input.leaderStill = true;
    for (int i = 0; i < 89; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::Follow);
    static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::FollowRetry);
    for (int i = 0; i < 40 && wolf.mode() != CompanionMode::Follow; ++i) static_cast<void>(wolf.step(input));
    REQUIRE(wolf.mode() == CompanionMode::Follow);
    for (int i = 0; i < 91; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::WarpOut);

    Companion beside;
    beside.arrive(false);
    settle(beside);
    auto near = input;
    near.playerDistance = 5;
    for (int i = 0; i < 300; ++i) static_cast<void>(beside.step(near));
    CHECK(beside.mode() == CompanionMode::Follow);
}

TEST_CASE("Reaching the player clears the follow failures") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.playerDistance = 30;
    input.standing = true;
    input.leaderAngle = 0;
    for (int i = 0; i < 90; ++i) static_cast<void>(wolf.step(input));
    REQUIRE(wolf.mode() == CompanionMode::FollowRetry);
    settle(wolf);
    REQUIRE(wolf.mode() == CompanionMode::Follow);
    for (int i = 0; i < 90; ++i) static_cast<void>(wolf.step(input));
    CHECK(wolf.mode() == CompanionMode::FollowRetry);
}

TEST_CASE("Howls carry for the original's alert time") {
    Companion wolf;
    wolf.arrive(false);
    auto input = calm();
    int alerts = 0, step = 0;
    for (; step < 80; ++step) alerts += wolf.step(input).alert ? 1 : 0;
    CHECK(alerts == 30);
}

TEST_CASE("Falling tells nearby animals to run, once") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = calm();
    input.hit = kMaxLife;
    int scatters = 0;
    for (int i = 0; i < 900 && !wolf.dead(); ++i) scatters += wolf.step(input).scatter ? 1 : 0;
    CHECK(wolf.dead());
    CHECK(scatters == 1);
    CHECK_FALSE(wolf.step(input).scatter);
}

TEST_CASE("The head stares while standing and glances while moving") {
    Companion wolf;
    wolf.arrive(false);
    auto input = calm();
    input.standing = true;
    input.leaderAngle = 0;
    settle(wolf, input);
    int looked = 0;
    for (int i = 0; i < 400; ++i) looked += wolf.step(input).look == CompanionAim::Leader ? 1 : 0;
    CHECK(looked >= 150);
    input.standing = false;
    looked = 0;
    for (int i = 0; i < 2000; ++i) looked += wolf.step(input).look == CompanionAim::Leader ? 1 : 0;
    CHECK(looked > 0);
    CHECK(looked < 800);
}

TEST_CASE("The head turns to the enemy in a fight and rests during scripted moves") {
    Companion wolf;
    wolf.arrive(false);
    settle(wolf);
    auto input = enemy(12);
    input.leaderAngle = 0;
    bool sawTarget = false;
    for (int i = 0; i < 1200; ++i) sawTarget |= wolf.step(input).look == CompanionAim::Target;
    CHECK(sawTarget);
}

TEST_CASE("A same-day summon brings back the life the wolf left with") {
    Companion hurt;
    hurt.arrive(false, 5);
    auto input = calm();
    input.life = 40;
    CHECK(hurt.step(input).setLife == 5);
    CHECK(hurt.life() == 5);

    Companion fallen;
    fallen.arrive(false, 0);
    CHECK(fallen.step(input).setLife == kMaxLife);

    Companion fresh;
    fresh.arrive(false);
    CHECK(fresh.step(input).setLife == kMaxLife);
}

TEST_CASE("The game's hit strength picks the knockdown, even when the tag shows just before the life drops") {
    const auto hit = [](std::uint8_t early, std::uint8_t strength, int lost, int gap = 0) {
        Companion wolf;
        wolf.arrive(false);
        settle(wolf);
        auto input = calm();
        input.animationDone = false;
        input.status.hitStrength = early;
        static_cast<void>(wolf.step(input));
        input.status.hitStrength = 0;
        for (int i = 0; i < gap; ++i) static_cast<void>(wolf.step(input));
        input.status.hitStrength = strength;
        input.hit = lost;
        return plays(wolf, input, "Dead");
    };
    CHECK(hit(3, 0, 1));
    CHECK(hit(0, 4, 1));
    CHECK(hit(0, 0, 8));
    CHECK_FALSE(hit(2, 2, 1));
    CHECK_FALSE(hit(0, 0, 1));
    CHECK_FALSE(hit(3, 0, 1, 8));
}
