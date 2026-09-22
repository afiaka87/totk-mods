// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include "ArrowHookshot.hpp"
#include "../../../pure/FlightClock.hpp"
#include "TargetValidator.hpp"
#include "WallGrip.hpp"

namespace arrowbound {
inline float bitsToFloat(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline std::uint32_t floatToBits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct AtomicVec3 {
    std::atomic<std::uint32_t> bits[3]{};

    void store(pure::Vec3 value) {
        bits[0].store(floatToBits(value.x), std::memory_order_relaxed);
        bits[1].store(floatToBits(value.y), std::memory_order_relaxed);
        bits[2].store(floatToBits(value.z), std::memory_order_relaxed);
    }

    pure::Vec3 load() const {
        return {bitsToFloat(bits[0].load(std::memory_order_relaxed)),
                bitsToFloat(bits[1].load(std::memory_order_relaxed)),
                bitsToFloat(bits[2].load(std::memory_order_relaxed))};
    }
};

struct DriveMailbox {
    std::atomic<std::uint32_t> active{0};
    std::atomic<std::uint32_t> updates{0};
    std::atomic<std::uint32_t> enters{0};
    std::atomic<std::uint32_t> leaves{0};
    std::atomic<std::uint32_t> fallUpdates{0};
    std::atomic<std::uint32_t> fallEnters{0};
    std::atomic<std::uint32_t> fallLeaves{0};
    std::atomic<std::uint32_t> fallActive{0};
    std::atomic<std::uint32_t> parasailActive{0};
    std::atomic<std::uint32_t> forceEntry{0};
    std::atomic<std::uint32_t> predForced{0};
    std::atomic<std::uint32_t> admissionWanted{0};
    std::atomic<std::uint32_t> admissionArms{0};
    std::atomic<std::uint32_t> captureActive{0};
    std::atomic<std::uint32_t> presentParaglider{0};
    std::atomic<std::uint32_t> poseState{0xFFFFFFFFu};
    std::atomic<std::uint32_t> poseEntryUpdates{0};
};

struct ArrowMailbox {
    std::atomic<std::uintptr_t> playerActor{0};
    std::atomic<std::uintptr_t> controllerToken{0};
    std::atomic<std::uint32_t> modeEnabled{0};
    std::atomic<std::uint32_t> acceptShots{0};
    std::atomic<std::uint32_t> pendingShot{0};
    std::atomic<std::uint32_t> shotSeq{0};
    std::atomic<std::uint32_t> shotSampleBase{0};
    std::atomic<std::uint32_t> shotHitBase{0};
    std::atomic<std::uint32_t> claimOldIgnored{0};
    std::atomic<std::uint32_t> claimOwnerMisses{0};
    std::atomic<std::uint32_t> sampleBodyMisses{0};
    std::atomic<std::uint32_t> sampleMotionWaits{0};
    std::atomic<std::uint32_t> impactChecks{0};
    std::atomic<std::uint32_t> impactPublished{0};
    std::atomic<std::uint32_t> sampleSeq{0};
    std::atomic<std::uint32_t> hitSeq{0};
    std::atomic<std::uint32_t> hitWallCandidate{0};
    AtomicVec3 samplePosition{};
    AtomicVec3 sampleVelocity{};
    AtomicVec3 hitPosition{};
    AtomicVec3 hitDirection{};
};

struct SessionState {
    std::uintptr_t base = 0;
    std::uint64_t tick = 0;
    std::uint32_t worldGen = 1;
};

struct WallGripMailbox {
    std::atomic<std::uint32_t> hooksReady{0};
    std::atomic<std::uint32_t> armed{0};
    std::atomic<std::uint32_t> climbUpdates{0};
    std::atomic<std::uint32_t> rejected{0};
    std::atomic<std::uint32_t> applied{0};
    std::atomic<std::uint32_t> inputApplied{0};
    std::atomic<std::uint32_t> npadId{0};
    std::atomic<std::uint32_t> npadValid{0};
    AtomicVec3 target{};
    AtomicVec3 normal{};
};

struct WallGripState {
    pure::Vec3 impact{};
    pure::Vec3 direction{};
    pure::Vec3 target{};
    pure::Vec3 normal{};
    bool confirmed = false;
    std::uint32_t request = 0;
    pure::WallGripWatch watch{};
};

struct AimState {
    std::uint32_t requestSeq = 0;
    int starved = 0;
    pure::TargetSample sample{};
    std::uint64_t sampleTick = 0;
};

struct ArrowTripState {
    pure::ArrowPhase phase = pure::ArrowPhase::Idle;
    std::uint32_t shotSeqSeen = 0;
    std::uint32_t sampleSeqSeen = 0;
    std::uint32_t hitSeqSeen = 0;
    std::uint32_t bailoutGlideUpdates = 0;
    std::uint64_t phaseTick = 0;
    std::uint64_t lastSampleTick = 0;
    pure::Vec3 arrowPosition{};
    pure::Vec3 arrowVelocity{};
    pure::ArrowFollower follower{};
    pure::FlightClockCursor clock{};
    pure::ArrowPredictionBudget prediction{};
    bool pendingMotionSample = false;
    WallGripState wall{};
    pure::Vec3 lastRequestedPosition{};
    float maxAcceptanceError = 0;
    bool haveRequestedPosition = false;
    float rotation[9]{};
    bool rotationValid = false;
};

struct HookshotRuntime {
    SessionState session{};
    AimState aim{};
    ArrowTripState arrowTrip{};
    pure::ArrowInputOwnership inputOwnership{};
    DriveMailbox drive{};
    ArrowMailbox arrow{};
    WallGripMailbox grip{};
    std::uint64_t lastButtons = 0;
    std::uint64_t prevButtons = 0;
    bool haveButtons = false;
};

HookshotRuntime& runtime();
void note(const char* text);
void resetSession(const char* reason);

}  // namespace arrowbound
