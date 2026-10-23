// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombState.hpp"

namespace remote_bomb::hud_policy {
enum class Action { Create, Creating, Detonate, Detonating, Recharge, Inventory };
struct View {
    Shape shape{};
    Upgrade upgrade{};
    Action action{};
    float remaining{}, progress = 1.f;
    bool visible{}, bombOut{};
};
inline View describe(const State &state, bool available, bool remembered, bool running) {
    const auto shape = detonationTarget(state);
    const auto &slot = state.slots[index(shape)];
    const bool bombOut = slot.phase == Phase::Creating || slot.phase == Phase::Live ||
                         slot.phase == Phase::DetonationRequested;
    // A bomb that is out shows its rune even after a native wheel choice: quick L still sets it off.
    View out{shape, state.upgrade, Action::Create, 0.f, 1.f,
             available && (remembered || bombOut) && running, bombOut};
    switch (slot.phase) {
    case Phase::Stored: out.action = Action::Inventory; break;
    case Phase::Creating: out.action = Action::Creating; break;
    case Phase::Live: out.action = Action::Detonate; break;
    case Phase::DetonationRequested: out.action = Action::Detonating; break;
    case Phase::Recharging:
        out.action = Action::Recharge;
        out.remaining = std::isfinite(slot.rechargeSeconds)
                            ? std::clamp(slot.rechargeSeconds, 0.f,
                                         abilityProfile(state.upgrade).rechargeSeconds)
                            : abilityProfile(state.upgrade).rechargeSeconds;
        out.progress = 1.f - out.remaining / abilityProfile(state.upgrade).rechargeSeconds;
        break;
    default: break;
    }
    return out;
}
// The rune shows only while a bomb is out, naming the bomb quick L sets off. After the blast it
// fades out with that bomb's icon rather than switching to the recharge gauge.
constexpr float kFadeInSeconds = .15f, kFadeOutSeconds = .5f;
struct Fade {
    View view{};
    float alpha{};
};
inline Fade fade(const Fade &previous, const View &view, float seconds) {
    const float step = std::isfinite(seconds) ? std::clamp(seconds, 0.f, 1.f) : 0.f;
    Fade next{view.bombOut ? view : previous.view,
              view.bombOut ? std::min(1.f, previous.alpha + step / kFadeInSeconds)
                           : std::max(0.f, previous.alpha - step / kFadeOutSeconds)};
    next.view.visible = view.visible && next.alpha > 0.f;
    return next;
}
}
