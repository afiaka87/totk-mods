// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "../src/wolf/SoundCues.hpp"
#include <string>
#include <vector>

using namespace botw_abilities::wolf;

namespace {
// The calls played on each of the given ticks, joined per tick.
std::vector<std::string> run(SoundTimeline& cues, int ticks, float delta = 1) {
    std::vector<std::string> played;
    for (int i = 0; i < ticks; ++i) {
        std::string tick;
        cues.advance(delta, [&](const SoundCue& cue) { tick += std::string(tick.empty() ? "" : ",") + cue.call; });
        played.push_back(tick);
    }
    return played;
}
}

TEST_CASE("Wolf chain lunge plays its three sounds on the tick it starts") {
    SoundTimeline cues;
    cues.start("Attack_ChainAttack", 1);
    const auto played = run(cues, 3);
    CHECK(played[0] == "ChainAttackWind,ChargeFootStep,RoarAttack");
    CHECK(played[1].empty());
    CHECK_FALSE(cues.pending());
}

TEST_CASE("Wolf bite rattles the chain once its clip reaches frame 3") {
    SoundTimeline cues;
    cues.start("Attack", 1);
    const auto played = run(cues, 6);
    CHECK(played == std::vector<std::string>{"", "", "", "ChainLarge", "", ""});
}

TEST_CASE("Wolf sound frames follow the clip's play rate") {
    SoundTimeline cues;
    cues.start("Turn_Attack", 2);
    const auto played = run(cues, 7);
    CHECK(played[5] == "Roar");
    CHECK(played[4].empty());
}

TEST_CASE("Wolf drops pending sounds when another command or a stop replaces the clip") {
    SoundTimeline cues;
    cues.start("Turn_Attack", 1);
    run(cues, 5);
    cues.start("Roar", 1);
    CHECK_FALSE(cues.pending());
    CHECK(run(cues, 20) == std::vector<std::string>(20));
    cues.start("Attack", 1);
    cues.stop();
    CHECK(run(cues, 5) == std::vector<std::string>(5));
    cues.start(nullptr, 1);
    CHECK_FALSE(cues.pending());
}
