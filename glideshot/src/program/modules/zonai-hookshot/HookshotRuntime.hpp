// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Named runtime state. The two mailboxes are the only channel between the gameplay tick and the
// action threads and carry plain values only.
#pragma once

#include <atomic>
#include <cstdint>

#include "CaptureMath.hpp"
#include "ChainVisual.hpp"
#include "HookshotState.hpp"
#include "LaunchInjector.hpp"
#include "PositionZip.hpp"
#include "TargetValidator.hpp"
#include "TransportMath.hpp"
#include "Vec3.hpp"
#include "YawEasing.hpp"

namespace zonai_hookshot {
inline float bitsToFloat(std::uint32_t bits) {
    float value;
    __builtin_memcpy(&value, &bits, sizeof(value));
    return value;
}

inline std::uint32_t floatToBits(float value) {
    std::uint32_t bits;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The tick writes target/speed before setting active; action hooks read them only while active.
struct DriveMailbox {
    std::atomic<std::uint32_t> active{0};
    float target[3]{};
    std::atomic<std::uint32_t> speedBits{0};
    std::atomic<std::uint32_t> updates{0};        // Parasail updates observed (ever)
    std::atomic<std::uint32_t> enters{0};         // Parasail enters observed (ever)
    std::atomic<std::uint32_t> leaves{0};         // Parasail leaves observed (ever)
    std::atomic<std::uint32_t> fallUpdates{0};    // Fall updates observed (ever)
    std::atomic<std::uint32_t> fallEnters{0};     // Fall enters observed (ever)
    std::atomic<std::uint32_t> fallLeaves{0};     // Fall leaves observed (ever)
    std::atomic<std::uint32_t> applied{0};        // velocity applications
    std::atomic<std::uint32_t> remainingBits{0};  // hook -> tick: metres to target
    std::atomic<std::uint32_t> observedBits{0};   // hook -> tick: |velocity| BEFORE our set
    std::atomic<std::uint32_t> jumpBoostArmed{0}; // one-shot: the jump hook may consume
    std::atomic<std::uint32_t> jumpPulses{0};     // synthetic-X samples mutated (this arm)
    std::atomic<std::uint32_t> jumpBoostHits{0};  // 4x consumptions (this arm)
    std::atomic<std::uint32_t> fallActive{0};     // native Fall currently owns Link
    std::atomic<std::uint32_t> parasailActive{0}; // native Parasail currently owns Link
    std::atomic<std::uint32_t> forceEntry{0};     // handoff armed: upgrade the predicate
    std::atomic<std::uint32_t> predForced{0};     // times the predicate flipped 0 -> 1
    std::atomic<std::uint32_t> admissionWanted{0};// position trip owns Parasail admission
    std::atomic<std::uint32_t> admissionArms{0};  // genuine Fall entries that armed it
    std::atomic<std::uint32_t> captureActive{0};  // Parasail update may apply terminal velocity
    float captureNormal[3]{};                     // published before captureActive
    std::atomic<std::uint32_t> captureApplied{0};
    std::atomic<std::uint32_t> captureBails{0};
    std::atomic<std::uint32_t> climbUpdates{0};   // authoritative Climb heartbeat
    float directionStart[3]{};
    float directionPrevious[3]{};
    std::atomic<std::uint32_t> directionSamples{0};
    std::atomic<std::uint32_t> directionAlignBits{0};
    std::atomic<std::uint32_t> directionSideBits{0};
    std::atomic<std::uint32_t> directionLateralBits{0};
};

// The tick publishes the live Npad id; the processed-controller hook owns capture-input telemetry.
struct WalkMailbox {
    std::atomic<std::uint32_t> targetNpadId{0};
    std::atomic<std::uint32_t> targetNpadValid{0};
    std::atomic<std::uint32_t> applications{0};
    std::atomic<std::uint32_t> otherControllerCalls{0};
    std::atomic<std::uint32_t> repeatedSamplingNumber{0};
    std::atomic<std::uint64_t> lastSamplingNumber{0};
    std::atomic<std::uint32_t> beforeXBits{0};
    std::atomic<std::uint32_t> beforeYBits{0};
};

struct SessionState {
    std::uintptr_t base = 0;
    std::uint64_t tick = 0;
    std::uint32_t worldGen = 1;
};

// Targeting: ray sequence, last consumed sample, and what a commit froze.
struct AimState {
    std::uint32_t requestSeq = 0;   // last issued request sequence
    int starved = 0;
    pure::TargetSample sample{};    // last consumed result
    std::uint64_t sampleTick = 0;
    std::uint32_t confirmFloorSeq = 0;
    pure::Verdict verdict = pure::Verdict::Pending;
    pure::Vec3 committedNormal{};
    std::uint32_t committedFlags = 0;
    float committedRange = 0.0f;
    std::uint32_t latchFeedbackTicks = 0;
};

// Transport bookkeeping (tick thread only).
struct TransportState {
    int tier = 0;                       // index into TransportConfig::speeds
    bool requested = false;
    std::uint64_t requestTick = 0;
    std::uint32_t fallUpdatesAtRequest = 0; // detach window: Fall activity since arm
    std::uint32_t fallLeavesAtCruise = 0;   // cruise guard: the Fall action left
    std::uint32_t lastFallUpdates = 0;      // cruise guard: Fall updates stalled
    std::uint32_t updatesAtHandoff = 0;     // handoff window: Parasail activity
    std::uint32_t leavesAtGlide = 0;        // terminal: the native glide closed
    int stallTicks = 0;
    std::uint64_t cruiseStartTick = 0;
    std::uint64_t handoffTick = 0;
    std::uint64_t glideStartTick = 0;
    pure::TripState trip{};
    char endReason[40] = "";
};

// The exact position carrier: the activation basis is immutable and only its world-up yaw is
// eased.
struct PositionDriveState {
    pure::PositionZipConfig config{};
    pure::PositionZipState path{};
    float baseRotation[9]{};
    pure::YawState yaw{};
    pure::Vec3 desiredFacing{};
    pure::Vec3 lastRequested{};
    bool haveRequested = false;
    // Link's position as the game exposed it at tick start, before the carrier wrote the next
    // step; the game draws him there, so the chain starts from it.
    pure::Vec3 shown{};
    bool haveShown = false;
    bool completed = false;
    bool failed = false;
    float lastAcceptanceError = 0.0f;
    float maxAcceptanceError = 0.0f;
    int handoffWaitTicks = 0;
};

struct CaptureState {
    std::uint64_t startedTick = 0;
    std::uint32_t climbUpdatesAtStart = 0;
    std::uint32_t parasailLeavesAtStart = 0;
    std::uint32_t lastParasailUpdates = 0;
    int stalledTicks = 0;
    int leaveGraceTicks = 0;
    float startDistance = 0.0f;
    int facingErrorMilliDegrees = -1;
    std::uint32_t inputLastReportedApplications = 0;
    char endReason[48] = "";
};

// The synthetic-X launch stage (tick thread only).
struct LaunchStage {
    pure::LaunchPulse pulse{};
    std::int64_t lastInjectSampling = 0;  // newest live sample the injector saw
    bool delayLogged = false;             // physical-X-held delay logged once
    std::uint32_t lastBoostHits = 0;      // JUMP_BOOST_CONSUMED edge detector
};

struct HookshotRuntime {
    SessionState session{};
    AimState aim{};
    pure::Machine machine{};
    pure::LaunchState launch{};
    TransportState transport{};
    PositionDriveState positionDrive{};
    CaptureState capture{};
    LaunchStage launchStage{};
    DriveMailbox drive{};
    WalkMailbox walk{};

    pure::YawTiming yawTiming = pure::YawTiming::OnParasail;

    std::uint64_t lastButtons = 0;
    std::uint64_t prevButtons = 0;
    bool haveButtons = false;
};

HookshotRuntime& runtime();

// Log a player-facing state change.
void note(const char* text);

// Fail-closed reset from any phase; preserves the stick-click quarantine and the bench speed tier.
void resetSession(const char* reason);

}  // namespace zonai_hookshot
