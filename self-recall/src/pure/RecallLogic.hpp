#pragma once

#include <cstdint>

namespace self_recall::pure {

constexpr std::uint16_t kHistoryCapacity = 3840;
constexpr float kNominalSampleRate = 60.0f;

struct HoldState {
    std::uint8_t ticks = 0;
    bool fired = false;
};

constexpr bool holdStep(HoldState& state, bool held, std::uint8_t threshold) {
    if (!held) {
        state = {};
        return false;
    }
    if (state.fired) return false;
    if (state.ticks < threshold) ++state.ticks;
    if (state.ticks < threshold) return false;
    state.fired = true;
    return true;
}

constexpr std::uint16_t previous(std::uint16_t index,
                                 std::uint16_t capacity = kHistoryCapacity) {
    return index == 0 ? std::uint16_t(capacity - 1)
                      : std::uint16_t(index - 1);
}

constexpr std::uint16_t clampTickDelta(std::uint64_t delta) {
    if (delta == 0) return 1;
    return delta > 0xffffu ? 0xffffu : static_cast<std::uint16_t>(delta);
}

constexpr std::uint16_t replayDelayAfterApply(std::uint16_t recordedDelta) {
    return recordedDelta > 1 ? std::uint16_t(recordedDelta - 1) : 0;
}

constexpr float recordedPathSpeed(float distanceMeters,
                                  std::uint16_t tickDelta) {
    return tickDelta == 0 ? 0.0f
                          : distanceMeters * kNominalSampleRate /
                                static_cast<float>(tickDelta);
}

constexpr bool isTeleportDiscontinuity(float distanceMeters,
                                       float thresholdMeters = 30.0f) {
    return distanceMeters > thresholdMeters;
}

constexpr float kEngineStepHz = 30.0f;
constexpr float kDistinctStepMeters = 0.001f;

constexpr float carriedPathSpeed(float lastDistinctSpeed,
                                 std::uint64_t ticksSinceDistinct,
                                 std::uint64_t inheritLimit = 2) {
    return ticksSinceDistinct <= inheritLimit ? lastDistinctSpeed : 0.0f;
}

constexpr float kCorrectionBaseMeters = 0.75f;
constexpr int kBlockPersistTicks = 6;

constexpr float correctionAllowanceMeters(float recordedPathSpeed,
                                          float recordedEngineSpeed,
                                          float liveEngineSpeed) {
    float fastest = recordedPathSpeed;
    if (recordedEngineSpeed > fastest) fastest = recordedEngineSpeed;
    if (liveEngineSpeed > fastest) fastest = liveEngineSpeed;
    const float positive = fastest > 0.0f ? fastest : 0.0f;
    return kCorrectionBaseMeters + positive / kEngineStepHz;
}

constexpr std::uint16_t kIdleCropUpdates = 180;

enum class RecordDecision : std::uint8_t {
    Record,
    SkipIdle,
    ResumeAfterCrop,
};

struct IdleCropState {
    std::uint16_t stationaryUpdates = 0;
    bool skipping = false;
};

constexpr RecordDecision idleCropStep(IdleCropState& state, bool moved,
                                      std::uint16_t limit = kIdleCropUpdates) {
    if (moved) {
        const bool resumed = state.skipping;
        state.skipping = false;
        state.stationaryUpdates = 0;
        return resumed ? RecordDecision::ResumeAfterCrop
                       : RecordDecision::Record;
    }
    if (state.skipping) return RecordDecision::SkipIdle;
    if (state.stationaryUpdates < 0xffffu) ++state.stationaryUpdates;
    if (state.stationaryUpdates > limit) {
        state.skipping = true;
        return RecordDecision::SkipIdle;
    }
    return RecordDecision::Record;
}

constexpr float kIdleTranslationMeters = 0.01f;
constexpr std::int32_t kIdleYawMilliDegrees = 500;

constexpr bool idleMotionExceeded(float translationMeters,
                                  std::int32_t yawDeltaMilliDegrees,
                                  bool stateClassChanged) {
    const std::int32_t yawMagnitude = yawDeltaMilliDegrees < 0
                                          ? -yawDeltaMilliDegrees
                                          : yawDeltaMilliDegrees;
    return stateClassChanged ||
           translationMeters >= kIdleTranslationMeters ||
           yawMagnitude >= kIdleYawMilliDegrees;
}


consteval bool contracts() {
    HoldState hold{};
    constexpr std::uint8_t threshold = 45;
    for (std::uint8_t i = 1; i < threshold; ++i) {
        if (holdStep(hold, true, threshold)) return false;
    }
    if (!holdStep(hold, true, threshold) || holdStep(hold, true, threshold))
        return false;
    holdStep(hold, false, threshold);
    if (hold.ticks != 0 || hold.fired) return false;

    if (previous(0) != 3839 || previous(1) != 0 ||
        previous(3839) != 3838)
        return false;
    if (clampTickDelta(0) != 1 || clampTickDelta(12) != 12 ||
        clampTickDelta(70000) != 0xffff)
        return false;
    if (replayDelayAfterApply(1) != 0 ||
        replayDelayAfterApply(4) != 3)
        return false;
    if (recordedPathSpeed(1.0f, 60) != 1.0f ||
        recordedPathSpeed(2.0f, 60) != 2.0f)
        return false;
    if (!isTeleportDiscontinuity(31.0f) ||
        isTeleportDiscontinuity(30.0f))
        return false;

    if (correctionAllowanceMeters(0.0f, 0.0f, 0.0f) != kCorrectionBaseMeters)
        return false;
    if (!(correctionAllowanceMeters(0.0f, 53.9f, 107.4f) > 1.79f))
        return false;
    if (!(correctionAllowanceMeters(1.5f, 1.0f, 1.5f) < 0.89f)) return false;

    if (recordedPathSpeed(0.1f, 2) != carriedPathSpeed(3.0f, 1) ||
        carriedPathSpeed(3.0f, 2) != 3.0f ||
        carriedPathSpeed(3.0f, 3) != 0.0f)
        return false;

    IdleCropState idle{};
    for (int i = 0; i < 180; ++i) {
        if (idleCropStep(idle, false) != RecordDecision::Record)
            return false;
    }
    if (idleCropStep(idle, false) != RecordDecision::SkipIdle) return false;
    if (idleCropStep(idle, false) != RecordDecision::SkipIdle) return false;
    if (idleCropStep(idle, true) != RecordDecision::ResumeAfterCrop)
        return false;
    if (idleCropStep(idle, true) != RecordDecision::Record) return false;

    if (idleMotionExceeded(0.0f, 0, false)) return false;
    if (!idleMotionExceeded(0.01f, 0, false)) return false;
    if (!idleMotionExceeded(0.0f, -500, false)) return false;
    if (!idleMotionExceeded(0.0f, 0, true)) return false;

    return true;
}

static_assert(contracts(), "Self Recall pure contracts must remain exact");

}  // namespace self_recall::pure
