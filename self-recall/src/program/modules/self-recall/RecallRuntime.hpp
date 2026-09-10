#pragma once

#include <cstdint>

#include "RecallAnimationPolicy.hpp"
#include "RecallHistory.hpp"
#include "RecallLogic.hpp"
#include "RecallOutfitSpeed.hpp"
#include "RecallInput.hpp"
#include "RouteProbe.hpp"

namespace self_recall {

enum class Unsafe : std::uint8_t {
    None,
    Loading,
    Riding,
    SpecialState,
    Tumbling,
    StateUnreadable,
};

const char* unsafeName(Unsafe unsafe);

struct SessionState {
    pure::OutfitSpeed speed{};
    std::uintptr_t mainBase = 0;
    std::uint64_t tick = 0;
    std::uint32_t worldGeneration = 1;
    char event[96] = "run a route, then hold ZL+Down";
    char status[128]{};
    char routeLine[128] = "History: warming up";
};

struct SafetyState {
    std::uint64_t lastClimbSeen = 0;
    bool climbActiveNow = false;
    bool controlStickActiveNow = false;
    Unsafe unsafeNow = Unsafe::Loading;
    std::uint32_t uiStateRaw = 0;
    probe::ProbeState probe{};
};

struct PlaybackState {
    pure::HoldState triggerHold{};
    bool swallowB = false;

    bool rewinding = false;
    bool startPending = false;
    bool selectionPending = false;
    bool selectedEnd = false;
    std::uint64_t lastStepClockSerial = 0;
    std::uint64_t startedNanoseconds = 0;
    std::uint16_t rewindIndex = 0;
    std::uint16_t rewindRemaining = 0;
    std::uint16_t rewindTotal = 0;
    int fightTicks = 0;
    bool haveApplied = false;
    pure::Pose lastApplied{};
    float lastAppliedRecordedSpeed = 0.0f;
    float lastAppliedEngineSpeed = 0.0f;
    pure::PlaybackRate lastAppliedRate = pure::PlaybackRate::Normal;
    std::uint8_t lastAnimKind = pure::kKindNone;
    std::uint8_t lastAnimSlot = pure::kInvalidSlot;
    std::uint8_t lastSampleFlags = 0;
    std::int32_t lastAnimStickX = 0;
    std::int32_t lastAnimStickY = 0;
    bool haveVerified = false;
    pure::Pose lastVerified{};
    float replayMeters = 0.0f;
    float replayPeakRecordedSpeed = 0.0f;
    std::int32_t startYawMilliDegrees = 0;
    std::int32_t lastYawMilliDegrees = 0;
};

struct RecallRuntime {
    SessionState session{};
    input::InputState input{};
    PlaybackState playback{};
    SafetyState safety{};
};

RecallRuntime& runtime();

void setEvent(const char* text);

void clearRoute(const char* reason);

}  // namespace self_recall
