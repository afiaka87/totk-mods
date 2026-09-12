#pragma once

#include <cstdint>

#include "RecallVisual.hpp"
#include "RecallBase.hpp"
#include "RecallRuntimeEngine.hpp"
#include "RecallModelEngine.hpp"

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
    std::uint64_t tick = 0;
    std::uint32_t worldGeneration = 1;
    char event[96] = "run a route, then hold ZL+RStick";
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
    std::uint16_t rewindRemaining = 0;
    std::uint16_t rewindTotal = 0;
    int fightTicks = 0;
    bool haveApplied = false;
    pure::Pose lastApplied{};
    float lastAppliedRecordedSpeed = 0.0f;
    float lastAppliedEngineSpeed = 0.0f;
    pure::PlaybackRate lastAppliedRate = pure::PlaybackRate::Normal;
    std::uint8_t lastSampleFlags = 0;
    bool haveVerified = false;
    pure::Pose lastVerified{};
};

struct RecallRuntime {
    SessionState session{};
    PlaybackState playback{};
    SafetyState safety{};
};

RecallRuntime& runtime();

void setEvent(const char* text);

void clearRoute(const char* reason);

}

#include "totk/engine/Npad.hpp"

namespace self_recall::playback {

void start(RecallRuntime& runtime);

void step(RecallRuntime& runtime);

void finish(RecallRuntime& runtime, pure::PlaybackStop stop,
            bool emitEnd);

void applyRecordedInput(const totk::engine::NpadFrame& frame);

void clearVelocity(const char* reason);

}

namespace self_recall::effects {

void buildRoute();

bool startRewind();

void stopForExit(const char* reason, bool emitEnd);

void stop(const char* reason);

}

#include <lib.hpp>

#define SRLOG(...) Logging.Log("[self-recall] " __VA_ARGS__)

namespace self_recall::presentation {

void initialize(std::uintptr_t mainBase);
bool start(void* playerActor, std::uint32_t historyGeneration);
void stop(void* playerActor, const char* reason, bool emitEnd);
void service();

}

#include "totk/harness/Module.hpp"

namespace self_recall::safety {

constexpr int kWarmupTicks = 30;
constexpr int kClimbGraceTicks = 45;
constexpr float kUprightMin = 0.7f;

Unsafe classify(RecallRuntime& runtime);

void serviceProbe(RecallRuntime& runtime);
void armProbe(RecallRuntime& runtime);

void observeRaycast(wwpg::RaycastFn original, const void* from,
                    const void* object);

}

namespace wwpg::modules {
const Module& selfRecall();
}
