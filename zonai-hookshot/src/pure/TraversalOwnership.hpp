// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

namespace zonai_hookshot::pure {
struct TraversalInput {
    bool manualActive = false;
    bool arrowActive = false;
    bool fresh = false;
    bool bowHeld = false;
    bool chordHeld = false;
    bool cancelHeld = false;
};

struct TraversalDecision {
    bool yieldManual = false;
    bool yieldArrow = false;
    bool allowManual = false;
    bool allowArrow = false;
};

inline TraversalDecision traversalOwnership(TraversalInput in) {
    TraversalDecision out{};
    // B belongs to the existing owner. Bow wins simultaneous bow/chord intent.
    if (in.fresh && !in.cancelHeld) {
        out.yieldManual = in.manualActive && in.bowHeld;
        out.yieldArrow = in.arrowActive && in.chordHeld && !in.bowHeld;
    }
    const bool manual = in.manualActive && !out.yieldManual;
    const bool arrow = in.arrowActive && !out.yieldArrow;
    out.allowManual = !arrow && !in.bowHeld && !in.cancelHeld;
    // A chord reserves the handoff before the manual state machine arms.
    out.allowArrow = !manual && !(out.allowManual && in.chordHeld);
    return out;
}

template <class YieldManual, class YieldArrow, class TickArrow, class TickManual>
void dispatchTraversal(const TraversalDecision& decision, YieldManual yieldManual,
                       YieldArrow yieldArrow, TickArrow tickArrow, TickManual tickManual) {
    if (decision.yieldManual) yieldManual();
    if (decision.yieldArrow) yieldArrow();
    // Retire old movement before servicing the new owner; mask cancel B before manual input.
    tickArrow(decision.allowArrow);
    tickManual(decision.allowManual);
}
}
