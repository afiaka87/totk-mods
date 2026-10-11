// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisInput.hpp"
#include "CryonisCore.hpp"
namespace cryonis::control {
enum class AimChange { None, Enter, Exit };
enum class Action { None, Cancel, CreationPending, Shatter, Construct };
struct Memory {
    input::Activation activation;
    input::RecentAbility recent;
    // L was pressed while another ability held it and is still down.
    bool yieldedPress{};
};
struct Context {
    input::Wheel wheel;
    std::uint64_t buttons{}, pressed{};
    bool fresh{}, running{}, swimming{}, aiming{}, pending{}, otherHoldsL{};
};
struct Transition {
    Memory next;
    input::Decision chord;
    AimChange aim{};
    bool quick{};
};
inline Transition transition(const Memory &previous, const Context &context) {
    Transition result;
    result.next = previous;
    const bool canEnter = !context.swimming && !context.aiming && !context.pending;
    result.chord = result.next.activation.step(context.wheel, context.buttons, context.pressed,
                                               context.fresh, context.running, canEnter);
    const bool chordOwnsL = result.next.activation.suppressL || result.next.activation.pendingEpoch;
    result.quick = result.next.recent.step(
        context.wheel, context.buttons, context.pressed, context.fresh, context.running,
        !context.swimming && !context.pending, chordOwnsL || context.otherHoldsL);
    // The other ability hands L back on a press, so that same press is this ability's tap.
    if (context.fresh && (context.pressed & input::L) && context.otherHoldsL)
        result.next.yieldedPress = true;
    else if (!(context.buttons & input::L))
        result.next.yieldedPress = false;
    else if (result.next.yieldedPress && context.fresh && !context.otherHoldsL && !chordOwnsL &&
             context.running && !context.swimming && !context.pending && !context.wheel.open &&
             !(context.buttons & input::B)) {
        result.next.yieldedPress = false;
        result.next.recent.adopt(context.wheel.epoch);
    }
    if (result.quick && context.aiming)
        result.aim = AimChange::Exit;
    else if (result.chord.beginAim || result.quick) {
        result.aim = AimChange::Enter;
        result.next.recent.remember();
    }
    return result;
}
inline Action action(bool aiming, bool running, std::uint64_t pressed, bool chordEntered,
                     bool pending, Target target) {
    if (!aiming || !running || chordEntered)
        return Action::None;
    if (pressed & input::B)
        return Action::Cancel;
    if (!(pressed & input::A))
        return Action::None;
    if (pending)
        return Action::CreationPending;
    return target.valid && target.pillar >= 0 ? Action::Shatter : Action::Construct;
}
}
