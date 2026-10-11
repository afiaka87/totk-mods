// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombHudPolicy.hpp"
#include "BombEffects.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
unsigned checks{};
void require(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
void lifecycle(remote_bomb::Upgrade upgrade) {
    using namespace remote_bomb;
    using namespace hud_policy;
    State state;
    state.upgrade = upgrade;
    const auto snapshot = [&] { return describe(state, true, true, true); };
    require(snapshot().action == Action::Create && snapshot().progress == 1.f,
            "a ready selected bomb advertises creation");
    const auto create = transition(state, {Event::UseSelected});
    state = create.next;
    require(snapshot().action == Action::Creating, "asynchronous creation has no repeated use prompt");
    state = transition(state, {Event::Provisioned, Shape::Sphere, create.command.ticket}).next;
    require(snapshot().action == Action::Inventory, "a granted bomb waits in the pouch");
    state = transition(state, {Event::InventoryActorCreated, Shape::Sphere}).next;
    const auto live = state.slots[0].ticket;
    require(snapshot().action == Action::Detonate, "an adopted bomb advertises detonation");
    state = transition(state, {Event::UseSelected}).next;
    require(snapshot().action == Action::Detonating, "pending native detonation stays pending in HUD");
    state = transition(state, {Event::DetonationFailed, Shape::Sphere, live}).next;
    require(snapshot().action == Action::Detonate, "failed detonation becomes retryable in HUD");
    state = transition(state, {Event::Exploded, Shape::Sphere, live}).next;
    const auto duration = abilityProfile(upgrade).rechargeSeconds;
    require(snapshot().action == Action::Recharge && snapshot().remaining == duration &&
                snapshot().progress == 0.f, "original profile duration starts empty recharge");
    state = advanceRecharge(state, duration / 2.f);
    require(snapshot().progress == .5f, "original profile recharge reaches halfway");
    const auto paused = advanceRecharge(state, 0.f);
    require(describe(paused, true, true, true).remaining == snapshot().remaining,
            "paused gameplay does not alter recharge presentation");
    state = transition(state, {Event::SelectCube}).next;
    require(snapshot().action == Action::Create && snapshot().shape == Shape::Cube,
            "other shape remains ready during sphere recharge");
    state = transition(state, {Event::SelectSphere}).next;
    require(snapshot().action == Action::Recharge && snapshot().progress == .5f,
            "returning to sphere restores its independent gauge");
    state = advanceRecharge(state, duration);
    require(snapshot().action == Action::Create && snapshot().progress == 1.f &&
                snapshot().upgrade == upgrade, "completed original recharge restores creation prompt");
}
void visibility() {
    using namespace remote_bomb;
    using namespace hud_policy;
    State state;
    require(!describe(state, false, true, true).visible, "loading hides the ability HUD");
    require(!describe(state, true, false, true).visible, "native ability selection hides custom HUD");
    require(!describe(state, true, true, false).visible, "paused menus hide gameplay labels");
    require(describe(state, true, true, true).visible, "live remembered ability shows labels");
    state.slots[0].phase = Phase::Recharging;
    state.slots[0].rechargeSeconds = std::numeric_limits<float>::quiet_NaN();
    require(describe(state, true, true, true).progress == 0.f, "invalid remaining time cannot reach GPU");
    state.slots[0].rechargeSeconds = -1.f;
    require(describe(state, true, true, true).progress == 1.f, "negative remaining time is bounded");
    state.slots[0].rechargeSeconds = 99.f;
    require(describe(state, true, true, true).progress == 0.f, "oversized remaining time is bounded");
}
void fading() {
    using namespace remote_bomb;
    using namespace hud_policy;
    State state;
    Fade shown;
    shown = fade(shown, describe(state, true, true, true), 1.f);
    require(!shown.view.visible && shown.alpha == 0.f, "the rune stays hidden while no bomb is out");
    state.slots[0].phase = Phase::Live;
    shown = fade(shown, describe(state, true, true, true), kFadeInSeconds);
    require(shown.view.visible && shown.alpha == 1.f && shown.view.action == Action::Detonate,
            "a bomb out shows the rune of the bomb quick L sets off");
    require(!fade(shown, describe(state, true, true, false), 0.f).view.visible,
            "paused menus still hide the rune at once");
    state.slots[0].phase = Phase::Recharging;
    state.slots[0].rechargeSeconds = 3.f;
    shown = fade(shown, describe(state, true, true, true), kFadeOutSeconds / 2);
    require(shown.view.visible && shown.alpha == .5f && shown.view.action == Action::Detonate,
            "after the blast the exploded bomb's rune fades without a recharge gauge");
    shown = fade(shown, describe(state, true, true, true), std::numeric_limits<float>::quiet_NaN());
    require(shown.alpha == .5f, "an invalid clock step cannot jump the fade");
    shown = fade(shown, describe(state, true, true, true), kFadeOutSeconds);
    require(!shown.view.visible && shown.alpha == 0.f, "the fade finishes hidden");
}
void effectsTimeline() {
    using namespace remote_bomb;
    using namespace effects;
    const Ticket sphere{1,2}, cube{1,3};
    Timeline state;
    state = begin(state, Shape::Sphere, {1,2,3}, sphere);
    state = begin(state, Shape::Cube, {4,5,6}, cube);
    require(state.bursts[0].active && state.bursts[1].active, "both bomb creation effects can coexist");
    require(state.bursts[0].position[1] == 2.3f, "creation uses original ELink height offset");
    state = advance(state, 3.f);
    state = begin(state, Shape::Sphere, {1,2,3}, sphere);
    require(state.bursts[0].frames == 3.f, "repeated observations do not restart an effect");
    require(advance(state, 0.f).bursts[0].frames == 3.f, "pause freezes the effects clock");
    require(advance(state, std::numeric_limits<float>::quiet_NaN()).bursts[0].frames == 3.f,
            "invalid frame time cannot poison effect uniforms");
    state = track(state, Shape::Sphere, sphere, {1,4,3}, true);
    require(state.bursts[0].position[1] == 4.3f && state.live[0], "appearance follows a carried bomb");
    state = track(state, Shape::Sphere, cube, {9,9,9}, true);
    require(state.bursts[0].position[0] == 1.f, "another bomb's position never moves the burst");
    state = track(state, Shape::Sphere, {}, {}, false);
    require(!state.live[0] && state.bursts[0].active, "a removed bomb leaves its burst to finish");
    state = advance(state, 20.f);
    require(!state.bursts[0].active && !state.bursts[1].active, "one-shot effects expire without live actors");
    require(!visible(state) && visible(track(state, Shape::Cube, cube, {4,5,6}, true)),
            "a live bomb keeps the effects pass running");
    require(!visible(Timeline{}), "scene reset leaves no previous-scene particle positions");
    require(!begin({}, Shape::Sphere, {1,2,3}, {}).bursts[0].active, "unticketed effects are refused");
}
}
int main() {
    lifecycle(remote_bomb::Upgrade::Base);
    lifecycle(remote_bomb::Upgrade::Plus);
    visibility();
    fading();
    effectsTimeline();
    remote_bomb::State stored;
    stored.slots[0].phase = remote_bomb::Phase::Stored;
    const auto view = remote_bomb::hud_policy::describe(stored, true, true, true);
    require(view.action == remote_bomb::hud_policy::Action::Inventory,
            "stored native material advertises inventory handling rather than detonation");
    remote_bomb::State switched;
    switched.selected = remote_bomb::Shape::Cube;
    switched.slots[0].phase = remote_bomb::Phase::Live;
    switched.slots[1].phase = remote_bomb::Phase::Stored;
    const auto target = remote_bomb::hud_policy::describe(switched, true, true, true);
    require(target.shape == remote_bomb::Shape::Sphere && target.action == remote_bomb::hud_policy::Action::Detonate,
            "the prompt names the bomb quick L will set off after switching forms");
    std::cout << checks << " Remote Bomb presentation checks passed\n";
}
