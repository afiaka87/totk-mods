// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombInput.hpp"
#include <cstdlib>
#include <iostream>

namespace {
using namespace remote_bomb;
using namespace remote_bomb::input;
unsigned checks{};
void require(bool value, const char *message) {
    ++checks;
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
void quickTap() {
    State remembered;
    remembered.remembered = true;
    auto press = transition(remembered, {{7}, L, L, true, true, true});
    require(!press.use && press.next.tap, "quick L waits to distinguish a hold");
    auto staleSample = transition(press.next, {{7}, 0, 0, false, true, true});
    require(!staleSample.use && staleSample.next.tap, "reused input cannot complete a tap");
    auto use = transition(staleSample.next, {{7}, 0, 0, true, true, true});
    require(use.use && use.event == Event::Use, "fresh quick L release uses the bomb once");
    require(!transition(use.next, {{7}, 0, 0, true, true, true}).use,
            "held release cannot repeat native use");
}
void wheelSticksIgnored() {
    // Clicking either stick in the open wheel used to pick a bomb shape; it now does nothing.
    for (std::uint64_t stick : {1ull << 4, 1ull << 5, 3ull << 4}) {
        auto result = transition({}, {{7, true}, L | stick, stick, true, false, true});
        require(result.event == Event::None && !result.use && !result.next.remembered,
                "stick clicks in the open wheel choose nothing");
    }
}
void nativePriority() {
    auto remembered = State{};
    remembered.remembered = true;
    auto press = transition(remembered, {{4}, L, L, true, true, true});
    auto held = transition(press.next, {{5, true}, L, 0, true, false, true});
    require(!held.next.tap && held.next.remembered, "holding L opens native wheel without use");
    auto cancel = transition(held.next, {{5, false, true, true, true}, 0, 0, true, true, true});
    require(cancel.next.remembered && !cancel.use, "cancelled normal wheel preserves memory");
    auto accept = transition(cancel.next, {{6, false, true, false, true}, 0, 0, true, true, true});
    require(!accept.next.remembered && !accept.use, "any native selection replaces bomb ownership");
    require(gameplayKeys(kMappedAbilityKey | 3, true) == 3,
            "only native ability-use output is consumed while remembered");
    require(gameplayKeys(kMappedAbilityKey | 3, false) == (kMappedAbilityKey | 3),
            "native keys return unchanged after takeover");
    require(cancelNativeAmiibo(true, true, true, 0), "remembered bomb cancels native Amiibo aiming");
    require(!cancelNativeAmiibo(false, true, true, 0), "selected native Amiibo keeps its aiming");
    require(!cancelNativeAmiibo(true, false, true, 0), "paused Amiibo remains native");
    require(!cancelNativeAmiibo(true, true, false, 0), "other actors keep their Amiibo action");
    require(!cancelNativeAmiibo(true, true, true, 1) && !cancelNativeAmiibo(true, true, true, 2),
            "scan and drop processing are not cancelled by bomb memory");
    require(nativeCancelFlags(0x40000025) == 0x80000025 &&
                nativeCancelFlags(0x80000025) == 0x80000025,
            "native cancellation preserves ordinary flags and clears the conflicting success flag");
}
void pauseCancel() {
    auto sample = Sample{{4}, L, L, true, true, true};
    State state;
    state.remembered = true;
    state = transition(state, sample).next;
    auto pauseRelease = transition(state, {{4}, 0, 0, true, false, true});
    require(!pauseRelease.use && !pauseRelease.next.tap, "pause cancels an unfinished use gesture");
}
void bombOutAfterNativeChoice() {
    State state;
    state.remembered = true;
    Sample accept{{6, false, true, false, true}, 0, 0, true, true, true, true};
    accept.bombOut = true;
    state = transition(state, accept).next;
    require(!state.remembered, "a native wheel choice takes quick L while a bomb is out");
    Sample press{{6}, L, L, true, true, true};
    press.bombOut = true;
    auto result = transition(state, press);
    require(result.next.remembered && result.next.reclaimed && result.next.tap && !result.use,
            "quick L with a bomb still out takes L back for the bomb");
    auto release = press;
    release.buttons = release.pressed = 0;
    result = transition(result.next, release);
    require(result.use && result.event == Event::Use, "the release sets off the bomb that was left out");
    result = transition(result.next, release);
    require(result.next.remembered && !result.use, "native ability stays held off while the blast is pending");
    release.bombOut = false;
    result = transition(result.next, release);
    require(result.next.remembered && result.next.reclaimed, "L stays with the bomb after its blast until the next press");
    press.bombOut = false;
    result = transition(result.next, press);
    require(!result.next.remembered && !result.next.reclaimed && !result.next.tap && !result.use,
            "the next quick L with no bomb out hands L back to the native choice");
    result = transition(transition(result.next, release).next, press);
    require(!result.next.remembered && !result.next.tap, "quick L with no bomb out stays native");
    press.bombOut = true;
    press.wheel.open = true;
    require(!transition(state, press).next.remembered, "opening the wheel does not take L back");
    press.wheel.open = false;
    press.running = false;
    require(!transition(state, press).next.remembered, "paused input does not take L back");
    State held;
    held.remembered = held.reclaimed = true;
    accept.wheel.epoch = 9;
    result = transition(held, accept);
    require(!result.next.remembered && !result.next.reclaimed, "a later native choice ends the borrowed L");
}
void handBackToMod() {
    State state;
    state.remembered = state.reclaimed = true;
    Sample press{{6}, L, L, true, true, true};
    press.modChosen = true;
    auto result = transition(state, press);
    require(!result.next.remembered && !result.next.reclaimed && result.next.handing &&
                blocksNative(result.next) && !result.use,
            "handing L back to another mod's ability keeps the game's own ability off that press");
    auto held = press;
    held.pressed = 0;
    result = transition(result.next, held);
    require(result.next.handing && !result.use, "the block lasts while L stays down");
    auto release = press;
    release.buttons = release.pressed = 0;
    result = transition(result.next, release);
    require(!result.next.handing && !blocksNative(result.next) && !result.use, "letting go of L ends the block");
    press.modChosen = false;
    result = transition(state, press);
    require(!result.next.handing && !blocksNative(result.next), "a native choice gets L back on the same press");
}
}
int main() {
    quickTap();
    wheelSticksIgnored();
    nativePriority();
    pauseCancel();
    bombOutAfterNativeChoice();
    handBackToMod();
    std::cout << checks << " bomb input checks passed\n";
}
