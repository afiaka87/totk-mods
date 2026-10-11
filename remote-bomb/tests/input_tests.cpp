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
Sample open(std::uint64_t stick, std::uint32_t epoch = 7) {
    return {{epoch, true, true}, L | stick, stick, true, false, true};
}
void activation() {
    for (auto stick : {StickL, StickR}) {
        auto request = transition({}, open(stick));
        require(request.event == Event::RequestWheelCancel, "Map stick requests native cancellation");
        require(request.add == B && !(request.mask & L), "native wheel keeps L through cancellation");
        require(!request.use && !request.next.remembered, "opening chord never prematurely creates");
        auto wait = transition(request.next, {{7, false, false, true, true, true},
                                L | stick, 0, true, false, true});
        require(wait.event == Event::None && !wait.next.remembered,
                "paused wheel closure waits for live gameplay");
        auto equip = transition(wait.next, {{7, false, false, true, true, true},
                                 L | stick, 0, true, true, true});
        require(equip.event == Event::Equipped && equip.next.remembered,
                "cancelled closure equips in live gameplay");
        require(equip.next.selected == (stick == StickL ? Shape::Cube : Shape::Sphere),
                "left and right click select independent forms");
        require(!equip.use && (equip.mask & L), "chord release cannot create or detonate");
        auto release = transition(equip.next, {{7}, 0, 0, true, true, true});
        require(release.mask == 0 && !release.use, "physical chord release restores controls");
        auto press = transition(release.next, {{7}, L, L, true, true, true});
        require(!press.use && press.next.tap, "quick L waits to distinguish a hold");
        auto staleSample = transition(press.next, {{7}, 0, 0, false, true, true});
        require(!staleSample.use && staleSample.next.tap, "reused input cannot complete a tap");
        auto use = transition(staleSample.next, {{7}, 0, 0, true, true, true});
        require(use.use && use.event == Event::Use, "fresh quick L release uses selected form once");
        require(!transition(use.next, {{7}, 0, 0, true, true, true}).use,
                "held release cannot repeat native use");
    }
}
void nativePriority() {
    auto remembered = State{};
    remembered.remembered = true;
    auto press = transition(remembered, {{4}, L, L, true, true, true});
    auto held = transition(press.next, {{5, true, false}, L, 0, true, false, true});
    require(!held.next.tap && held.next.remembered, "holding L opens native wheel without use");
    auto cancel = transition(held.next, {{5, false, false, true, true, true}, 0, 0, true, true, true});
    require(cancel.next.remembered && !cancel.use, "cancelled normal wheel preserves memory");
    auto accept = transition(cancel.next, {{6, false, false, true, false, true}, 0, 0, true, true, true});
    require(!accept.next.remembered && !accept.use, "any native selection replaces bomb ownership");
    auto notMap = open(StickR);
    notMap.wheel.map = false;
    require(transition({}, notMap).event == Event::None, "Camera and Amiibo chords remain untouched");
    require(transition({}, open(StickL | StickR)).event == Event::None,
            "ambiguous dual click cannot choose a form");
    auto request = transition({}, open(StickR));
    auto explicitAccept = open(StickR);
    explicitAccept.nativeAccepted = true;
    auto takeover = transition(request.next, explicitAccept);
    require(!takeover.next.pendingEpoch && !takeover.next.remembered,
            "native accepted selection wins a pending chord race");
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
void failedHandoffs() {
    auto request = transition({}, open(StickL));
    auto stale = transition(request.next, {{8, true, true}, L, 0, true, false, true});
    require(stale.event == Event::StaleWheel && !stale.next.pendingEpoch,
            "a new wheel invalidates old cancellation");
    auto userCancel = transition(request.next, {{7, true, true}, L | B, B, true, false, true});
    require(userCancel.event == Event::UserCancelled && !userCancel.next.remembered,
            "physical B takes priority over injected cancel");
    auto unavailable = open(StickR);
    unavailable.available = false;
    require(transition({}, unavailable).event == Event::Unavailable,
            "missing live player does not request an activation");
    auto waiting = request.next;
    Decision result{};
    for (unsigned i = 0; i < 241; ++i) {
        result = transition(waiting, {{7, true, true}, L, 0, true, false, true});
        waiting = result.next;
    }
    require(result.event == Event::TimedOut && !waiting.pendingEpoch,
            "unacknowledged native cancellation cannot hold input forever");
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
    Sample accept{{6, false, false, true, false, true}, 0, 0, true, true, true, true};
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
    activation();
    nativePriority();
    failedHandoffs();
    bombOutAfterNativeChoice();
    handBackToMod();
    std::cout << checks << " bomb input checks passed\n";
}
