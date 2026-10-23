// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "HudPolicy.hpp"

using namespace botw_abilities::champions;
namespace {
States hudStates() {
    States states;
    for (unsigned i = 0; i < states.size(); ++i) states[i] = initial(static_cast<Blessing>(i));
    return states;
}
std::array<Context,4> liveContexts() { return {Context{true},Context{true},Context{true},Context{true}}; }
}

TEST_CASE("Champion HUD always shows enabled charges during gameplay") {
    auto states = hudStates();
    const auto contexts = liveContexts();
    CHECK(hud::makeFrame(states,contexts).count == 4);
    states[1] = consume(Blessing::Revali,states[1],contexts[1]).state;
    const auto used = hud::makeFrame(states,contexts);
    REQUIRE(used.count == 4);
    CHECK(used.items[1].icon == 1);
    CHECK(used.items[1].charges == 2);
    CHECK(used.items[1].capacity == 3);
    CHECK_FALSE(used.items[1].recharging);
    CHECK(used.items[1].tint[3] == 1);
    for (unsigned i = 0; i < 600; ++i) {
        const auto idle = hud::makeFrame(states,contexts);
        REQUIRE(idle.count == 4);
        CHECK(idle.items[1].charges == 2);
        CHECK(idle.items[1].tint[3] == 1);
    }
}

TEST_CASE("Champion recharge HUD persists across loading and fills against the base timer under every rate") {
    auto states = hudStates();
    const auto contexts = liveContexts();
    for (unsigned i = 0; i < states.size(); ++i) {
        states[i].charges = 0;
        states[i].remainingSeconds = rule(static_cast<Blessing>(i)).rechargeSeconds*0.5f;
    }
    const auto frame = hud::makeFrame(states,contexts);
    REQUIRE(frame.count == 4);
    for (unsigned i = 0; i < frame.count; ++i) {
        CHECK(frame.items[i].icon == i);
        CHECK(frame.items[i].progress == doctest::Approx(0.5f));
        CHECK(frame.items[i].recharging);
        CHECK(frame.items[i].charges == 0);
        CHECK(frame.items[i].tint[3] == 1);
    }
    auto faster = contexts;
    for (auto& context : faster) { context.ballad = true; context.castle = true; }
    const auto same = hud::makeFrame(states,faster);
    for (unsigned i = 0; i < same.count; ++i) CHECK(same.items[i].progress == frame.items[i].progress);
    const auto ready = hud::makeFrame(hudStates(),contexts);
    REQUIRE(ready.count == 4);
    for (const auto& item : ready.items) {
        CHECK_FALSE(item.recharging);
        CHECK(item.progress == 1);
        CHECK(item.charges == item.capacity);
    }
}

TEST_CASE("Champion HUD hides in menus shrines and disabled states then immediately returns") {
    auto states = hudStates();
    auto contexts = liveContexts();
    REQUIRE(hud::makeFrame(states,contexts).count == 4);
    for (auto& context : contexts) context.gameplay = false;
    CHECK(hud::makeFrame(states,contexts).count == 0);
    for (auto& context : contexts) context = {true,true};
    CHECK(hud::makeFrame(states,contexts).count == 0);
    contexts = liveContexts();
    states[0].enabled = false;
    const auto partial = hud::makeFrame(states,contexts);
    REQUIRE(partial.count == 3);
    CHECK(partial.items[0].icon == 1);
    states[0].enabled = true;
    CHECK(hud::makeFrame(states,contexts).count == 4);
}

TEST_CASE("All six custom ability cells use one aligned row without overlapping rings") {
    using namespace totk::render::ability_hud;
    for (unsigned i = 0; i < 6; ++i) {
        const auto box = rect(i);
        CHECK(box[1] == doctest::Approx(122.4f));
        CHECK(box[2] == 22);
        CHECK(box[3] == 22);
        if (i) CHECK(rect(i-1)[0]+halfSize < box[0]-halfSize);
    }
    CHECK(rect(0)[0] == doctest::Approx(134.4f));
    CHECK(rect(5)[0]+halfSize < 430);
}
