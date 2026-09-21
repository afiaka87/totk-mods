// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Pure state machine.

// Shipped route: target, fire, latch, exact position cruise, capture; positionDriveEnabled=false keeps the older
// Fall/Parasail laboratory route host-testable. step() returns the buttons consumed on this tick.
#pragma once

#include <cstdint>

#include "TargetValidator.hpp"

namespace zonai_hookshot::pure {
enum class Phase : uint8_t {
    Idle,
    Arming,       // chord held, counting the deliberate hold
    Targeting,    // camera aiming with a live target diamond
    Confirming,   // A pressed: waiting (bounded) for a post-press fresh sample
    ChainLaunch,  // anchor frozen, chain extending outward
    Latched,       // one-tick latch beat before automatic transport
    PositionCruise,// exact forceSetMatrix line drive to the 0.5 m standoff
    Capture,       // v0.3.6: short Parasail-hosted physical wall approach
    DetachRequest, // P2: native detach armed, awaiting an observed Fall update
    FallCruise,    // P2: native Fall owns Link, velocity driven to the anchor
    GlideHandoff,  // P2: near the anchor - drive stopped, Parasail entry enabled
    GlideTerminal, // P2: native Parasail active and untouched (the observation)
    Cooldown,
};

inline const char* phaseName(Phase p) {
    switch (p) {
        case Phase::Idle:          return "IDLE";
        case Phase::Arming:        return "ARMING";
        case Phase::Targeting:     return "TARGETING";
        case Phase::Confirming:    return "LOCKING";
        case Phase::ChainLaunch:   return "FIRING";
        case Phase::Latched:       return "LATCHED";
        case Phase::PositionCruise:return "POSITION ZIP";
        case Phase::Capture:       return "CAPTURE";
        case Phase::DetachRequest: return "DETACHING";
        case Phase::FallCruise:    return "ZIPPING";
        case Phase::GlideHandoff:  return "HANDOFF";
        case Phase::GlideTerminal: return "GLIDING";
        case Phase::Cooldown:      return "COOLDOWN";
    }
    return "?";
}

enum class Event : uint8_t {
    None,
    TargetingEntered,  // deliberate hold satisfied
    ArmingAbandoned,   // chord released before the hold completed
    ConfirmStarted,    // A pressed; module records the confirmation floor sequence
    Refused,           // confirmation failed (invalid target or timeout) - visible refusal
    Committed,         // post-press sample accepted; anchor frozen, ChainLaunch begins
    Latched,           // legacy diagnostic latch event (production auto-zips)
    Cancelled,         // B in Targeting/Confirming
    Cleared,           // B in ChainLaunch/Latched or any transport phase
    Reset,             // fail-closed world/scene/menu loss
    CooldownDone,
    DetachRequested,   // A in Latched; module arms the native detach lanes
    PositionZipStarted,// launch reached anchor; freeze start/line/endpoint
    PositionZipEnded,  // the exact 0.5 m standoff endpoint was applied
    PositionZipFailed, // v0.3.5 invalid/timeout guard fired
    CaptureStarted,    // exact carrier ended with native Parasail active
    ClimbAcquired,     // authoritative ExecutePlayerClimb update observed
    CaptureFailed,     // bounded terminal physical approach ended without Climb
    DetachRefused,     // detach window expired without an observed Fall - back to Latched
    FallEntered,       // Fall update observed - the fast zip begins
    TripAborted,       // FallCruise guard fired (reason recorded engine-side)
    HandoffArmed,      // inside handoff range - drive stopped, Parasail entry enabled
    GlideEntered,      // native Parasail admitted Link - passive terminal glide
    GlideRefused,      // handoff window expired without Parasail entry - trip ends
    GlideEnded,        // the native glide closed on its own - trip complete
};

struct MachineConfig {
    bool positionDriveEnabled = true;
    int armHoldTicks = 12;        // Porter/Self Recall deliberate-hold convention
    // Must exceed the ray mailbox timeout (24 ticks) so a post-press ray can answer; the module
    // static_asserts this.
    int confirmTimeoutTicks = 30;
    int cooldownTicks = 15;
    int detachAcquireTicks = 90;
    int glideAcquireTicks = 45;
};

struct MachineInputs {
    bool worldReady = false;     // player+camera resolved and stable
    bool freshInput = false;     // a controller sample arrived this tick
    bool chordHeld = false;       // ZL + right-stick click (last fresh sample)
    bool stickClickHeld = false;  // physical right-stick click (last fresh sample)
    bool aEdge = false;          // must be false when freshInput is false
    bool bEdge = false;          // must be false when freshInput is false
    ConfirmDecision confirm = ConfirmDecision::Waiting;  // only read in Confirming
    bool launchComplete = false; // ChainLaunch leading endpoint reached anchor
    bool fallEntered = false;    // a native Fall update was observed since the arm
    bool transportDone = false;  // a FallCruise guard fired (reason engine-side)
    bool handoffReached = false; // remaining distance inside the handoff range
    bool glideEntered = false;   // a native Parasail update observed since handoff
    bool glideDone = false;      // the native Parasail left (trip complete)
    bool positionZipDone = false;   // exact endpoint was applied last tick
    bool positionZipFailed = false; // invalid values or bounded timeout
    bool captureFailed = false;     // terminal timeout/Parasail-loss guard
    bool climbEntered = false;      // native Climb update observed after capture arm
};

struct Machine {
    Phase phase = Phase::Idle;
    int armTicks = 0;
    int confirmTicks = 0;
    int cooldownLeft = 0;
    int transportTicks = 0;    // P2 acquisition counter (detach + handoff windows)
    bool stickClickLatched = false;  // mask the stick click until physical release
    // Latched lasts one tick so B can still cancel and the renderer shows the latch beat before
    // Link moves.
    bool autoZipPending = false;
};

// Steady-state ownership by phase; the authoritative per-tick mask is StepOutput::consumed.
struct OwnedButtons {
    bool stickClick = false;
    bool a = false;
    bool b = false;
};

inline OwnedButtons ownedButtons(Phase p) {
    switch (p) {
        case Phase::Arming:      return {true, false, false};
        case Phase::Targeting:   return {false, true, true};
        case Phase::Confirming:  return {false, true, true};
        case Phase::ChainLaunch: return {false, false, true};
        case Phase::Latched:       return {false, true, true};
        case Phase::PositionCruise:return {false, true, true};
        case Phase::Capture:       return {false, true, true};
        case Phase::DetachRequest: return {false, true, true};
        case Phase::FallCruise:    return {false, true, true};
        case Phase::GlideHandoff:  return {false, true, true};
        case Phase::GlideTerminal: return {false, true, true};
        default:                   return {};
    }
}

struct StepOutput {
    Event event = Event::None;
    OwnedButtons consumed{};  // mask these from the game for THIS tick
};

inline StepOutput step(Machine& m, const MachineInputs& in, const MachineConfig& c = {}) {
    const Phase before = m.phase;

    if (in.freshInput && !in.stickClickHeld) m.stickClickLatched = false;

    Event ev = Event::None;
    if (!in.worldReady) {
        const bool wasActive = m.phase != Phase::Idle;
        const bool latch = m.stickClickLatched;
        m = {};
        m.stickClickLatched = latch;
        ev = wasActive ? Event::Reset : Event::None;
    } else {
        switch (m.phase) {
            case Phase::Idle:
                if (in.freshInput && in.chordHeld) {
                    m.phase = Phase::Arming;
                    m.armTicks = 0;
                    m.stickClickLatched = true;
                }
                break;
            case Phase::Arming:
                if (in.freshInput) {
                    if (!in.chordHeld) {
                        const bool latch = m.stickClickLatched;
                        m = {};
                        m.stickClickLatched = latch;
                        ev = Event::ArmingAbandoned;
                    } else if (++m.armTicks >= c.armHoldTicks) {
                        m.phase = Phase::Targeting;
                        m.armTicks = 0;
                        ev = Event::TargetingEntered;
                    }
                }
                break;
            case Phase::Targeting:
                if (in.bEdge) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::Cancelled;
                } else if (in.aEdge) {
                    m.phase = Phase::Confirming;
                    m.confirmTicks = 0;
                    ev = Event::ConfirmStarted;
                }
                break;
            case Phase::Confirming:
                if (in.bEdge) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::Cancelled;
                } else if (in.confirm == ConfirmDecision::Commit) {
                    m.phase = Phase::ChainLaunch;
                    ev = Event::Committed;
                } else if (in.confirm == ConfirmDecision::Refuse ||
                           ++m.confirmTicks >= c.confirmTimeoutTicks) {
                    m.phase = Phase::Targeting;
                    ev = Event::Refused;
                }
                break;
            case Phase::ChainLaunch:
                if (in.bEdge) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::Cleared;
                } else if (in.launchComplete) {
                    m.phase = Phase::Latched;
                    m.autoZipPending = true;
                    ev = Event::Latched;
                }
                break;
            case Phase::Latched:
                if (in.bEdge) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::Cleared;
                } else if (m.autoZipPending || in.aEdge) {
                    m.autoZipPending = false;
                    m.transportTicks = 0;
                    if (c.positionDriveEnabled) {
                        m.phase = Phase::PositionCruise;
                        ev = Event::PositionZipStarted;
                    } else {
                        m.phase = Phase::DetachRequest;
                        ev = Event::DetachRequested;
                    }
                }
                break;
            case Phase::PositionCruise:
                if (in.bEdge) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::Cleared;
                } else if (in.positionZipFailed) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::PositionZipFailed;
                } else if (in.positionZipDone && in.glideEntered) {
                    m.phase = Phase::Capture;
                    m.transportTicks = 0;
                    ev = Event::CaptureStarted;
                }
                break;
            case Phase::Capture:
                if (in.bEdge) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::Cleared;
                } else if (in.climbEntered) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::ClimbAcquired;
                } else if (in.captureFailed) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::CaptureFailed;
                }
                break;
            case Phase::DetachRequest:
                if (in.bEdge) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::Cleared;
                } else if (in.fallEntered) {
                    m.phase = Phase::FallCruise;
                    ev = Event::FallEntered;
                } else if (++m.transportTicks >= c.detachAcquireTicks) {
                    m.phase = Phase::Latched;
                    m.transportTicks = 0;
                    ev = Event::DetachRefused;
                }
                break;
            case Phase::FallCruise:
                // Handoff outranks the guards: reaching handoff range is the success path.
                if (in.bEdge) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::Cleared;
                } else if (in.handoffReached) {
                    m.phase = Phase::GlideHandoff;
                    m.transportTicks = 0;
                    ev = Event::HandoffArmed;
                } else if (in.transportDone) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::TripAborted;
                }
                break;
            case Phase::GlideHandoff:
                if (in.bEdge) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::Cleared;
                } else if (in.glideEntered) {
                    m.phase = Phase::GlideTerminal;
                    ev = Event::GlideEntered;
                } else if (++m.transportTicks >= c.glideAcquireTicks) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    m.transportTicks = 0;
                    ev = Event::GlideRefused;
                }
                break;
            case Phase::GlideTerminal:
                if (in.bEdge) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::Cleared;
                } else if (in.glideDone) {
                    m.phase = Phase::Cooldown;
                    m.cooldownLeft = c.cooldownTicks;
                    ev = Event::GlideEnded;
                }
                break;
            case Phase::Cooldown:
                if (--m.cooldownLeft <= 0) {
                    const bool latch = m.stickClickLatched;
                    m = {};
                    m.stickClickLatched = latch;
                    ev = Event::CooldownDone;
                }
                break;
        }
    }

    StepOutput out{};
    out.event = ev;
    const OwnedButtons pre = ownedButtons(before);
    const OwnedButtons post = ownedButtons(m.phase);
    out.consumed.stickClick = pre.stickClick || post.stickClick ||
                             (m.stickClickLatched && in.stickClickHeld);
    out.consumed.a = pre.a || post.a;
    out.consumed.b = pre.b || post.b;
    return out;
}

}  // namespace zonai_hookshot::pure
