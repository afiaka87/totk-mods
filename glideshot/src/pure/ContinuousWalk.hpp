// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Control state for the continuous-stick diagnostic chord; the engine hook owns the actual stick
// override.
#pragma once

namespace zonai_hookshot::pure {
struct ContinuousWalkConfig {
    int armSamples = 12;
    unsigned maxControllerApplications = 600;
};

struct ContinuousWalkControl {
    int heldSamples = 0;
    bool chordLatched = false;
};

enum class ContinuousWalkCommand : unsigned char { None, Start, Stop };

struct ContinuousWalkControlOutput {
    ContinuousWalkCommand command = ContinuousWalkCommand::None;
    bool consumeDpadUp = false;
    bool consumeB = false;
};

// Capture outranks the diagnostic so a wall approach never loses intent to the bench timer.
enum class ForwardOwner : unsigned char { None, DiagnosticWalk, WallCapture };

inline ForwardOwner selectForwardOwner(bool diagnosticWalkActive,
                                       bool wallCaptureActive,
                                       bool targetControllerValid) {
    if (!targetControllerValid) return ForwardOwner::None;
    if (wallCaptureActive) return ForwardOwner::WallCapture;
    if (diagnosticWalkActive) return ForwardOwner::DiagnosticWalk;
    return ForwardOwner::None;
}

inline bool forwardOwnerExpires(ForwardOwner owner, unsigned applications,
                                const ContinuousWalkConfig& config = {}) {
    return owner == ForwardOwner::DiagnosticWalk &&
           applications >= config.maxControllerApplications;
}

// `fresh` matters: a stale sample cannot arm this or synthesize a second edge.
inline ContinuousWalkControlOutput stepContinuousWalkControl(
    ContinuousWalkControl& state, bool fresh, bool chordHeld, bool bEdge,
    bool active, bool worldReady,
    const ContinuousWalkConfig& config = {}) {
    ContinuousWalkControlOutput out{};
    if (!fresh) return out;

    if (active && bEdge) {
        out.command = ContinuousWalkCommand::Stop;
        out.consumeB = true;
    }

    if (!chordHeld) {
        state.heldSamples = 0;
        state.chordLatched = false;
        return out;
    }

    out.consumeDpadUp = true;
    if (state.chordLatched || active || !worldReady || config.armSamples <= 0)
        return out;

    if (++state.heldSamples >= config.armSamples) {
        state.heldSamples = 0;
        state.chordLatched = true;
        out.command = ContinuousWalkCommand::Start;
    }
    return out;
}

}  // namespace zonai_hookshot::pure
