#pragma once

#include <cmath>
#include <cstdint>

#include "RecallLogic.hpp"
#include "totk/core/Units.hpp"

namespace self_recall::pure {

using Pose = totk::core::WorldTransform;
static_assert(sizeof(Pose) == 48,
              "recorded pose format: 9 rotation floats then 3 position floats");

constexpr std::uint16_t kMinHistory = 90;

enum SampleFlag : std::uint8_t {
    SampleAdmissible = 1u << 0,
    SampleClimb = 1u << 1,
    SampleNativeGlide = 1u << 2,
    SampleControlStick = 1u << 3,
};

struct HistorySample {
    Pose pose;
    float engineVelocity[3];
    float pathSpeed;
    std::uint64_t recordedTick;
    std::uint32_t serial;
    std::uint16_t tickDelta;
    std::uint8_t flags;
    std::uint8_t animKind;
    std::uint8_t animSlot;
    float animFrame;
    float animRate;
    std::int32_t stickX;
    std::int32_t stickY;
};
static_assert(sizeof(HistorySample) == 104, "recorded sample format is frozen");

struct RouteHistory {
    HistorySample samples[kHistoryCapacity];
    std::uint16_t head;
    std::uint16_t count;
    std::uint32_t nextSerial;
};

struct RecorderState {
    bool havePreviousRecorded;
    Pose previousRecorded;
    std::uint64_t previousRecordedTick;
    float latestRecordedSpeed;

    bool haveDistinct;
    float lastDistinctPosition[3];
    std::uint64_t lastDistinctTick;
    float lastDistinctSpeed;

    IdleCropState idleCrop;
    bool haveIdleAnchor;
    float idleAnchorPosition[3];
    std::int32_t idleAnchorYawMilliDegrees;
    std::uint8_t idleAnchorClass;
    std::uint64_t idleSkipStartTick;
};

struct RecordInput {
    Pose pose{};
    float engineVelocity[3]{};
    std::uint64_t tick = 0;
    std::uint8_t stateClass = 0;  // the live admissibility class (Unsafe)
    bool admissible = false;
    bool climbing = false;
    std::uint8_t animKind = 0;
    std::uint8_t animSlot = 0;
    float animFrame = 0.0f;
    float animRate = 0.0f;
    std::int32_t stickX = 0;
    std::int32_t stickY = 0;
};

enum class RecordResult : std::uint8_t {
    Recorded,
    ResumedAfterCrop,
    SkippedIdle,       // already cropping
    StartedCropping,   // first skipped update of this idle stretch
};

struct RecordReport {
    RecordResult result = RecordResult::Recorded;
    std::uint16_t count = 0;          // ring occupancy after the step
    bool reachedMinimum = false;      // count hit kMinHistory on this step
    bool reachedCapacity = false;     // count hit kHistoryCapacity on this step
    std::uint64_t skippedTicks = 0;   // only for ResumedAfterCrop
    std::uint16_t tickDelta = 0;
    float pathSpeed = 0.0f;
};

inline float distance3(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline float distance3(const totk::core::WorldPosition& a,
                       const totk::core::WorldPosition& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline float distance3(const totk::core::WorldPosition& a, const float b[3]) {
    const float dx = a.x - b[0];
    const float dy = a.y - b[1];
    const float dz = a.z - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline bool finitePose(const Pose& pose) {
    for (float value : pose.rotation.values)
        if (!std::isfinite(value)) return false;
    return std::isfinite(pose.position.x) && std::isfinite(pose.position.y) &&
           std::isfinite(pose.position.z);
}

inline std::int32_t yawMilliDegrees(const Pose& pose) {
    constexpr float kRadToMilliDegrees = 57295.7795f;
    const float yaw =
        std::atan2(-pose.rotation.values[2], -pose.rotation.values[8]);
    return std::isfinite(yaw)
               ? static_cast<std::int32_t>(yaw * kRadToMilliDegrees)
               : 0;
}

inline void clearRecorder(RecorderState& state) {
    state.havePreviousRecorded = false;
    state.previousRecordedTick = 0;
    state.latestRecordedSpeed = 0.0f;
    state.idleCrop = {};
    state.haveIdleAnchor = false;
    state.idleSkipStartTick = 0;
}

inline void clearHistory(RouteHistory& history) {
    history.head = 0;
    history.count = 0;
}

inline void clearSpeedWindow(RecorderState& state) {
    state.haveDistinct = false;
}

inline RecordReport recordSample(RecorderState& state, RouteHistory& history,
                                 const RecordInput& input) {
    RecordReport report{};

    bool moved = true;
    if (state.haveIdleAnchor) {
        const float translation =
            distance3(input.pose.position, state.idleAnchorPosition);
        std::int32_t yawDelta =
            yawMilliDegrees(input.pose) - state.idleAnchorYawMilliDegrees;
        if (yawDelta > 180000) yawDelta -= 360000;
        if (yawDelta < -180000) yawDelta += 360000;
        moved = idleMotionExceeded(
            std::isfinite(translation) ? translation : 1.0f, yawDelta,
            input.stateClass != state.idleAnchorClass);
    }
    const bool wasSkipping = state.idleCrop.skipping;
    const RecordDecision decision = idleCropStep(state.idleCrop, moved);
    if (moved || !state.haveIdleAnchor) {
        state.idleAnchorPosition[0] = input.pose.position.x;
        state.idleAnchorPosition[1] = input.pose.position.y;
        state.idleAnchorPosition[2] = input.pose.position.z;
        state.idleAnchorYawMilliDegrees = yawMilliDegrees(input.pose);
        state.idleAnchorClass = input.stateClass;
        state.haveIdleAnchor = true;
    }
    if (decision == RecordDecision::SkipIdle) {
        if (!wasSkipping) state.idleSkipStartTick = input.tick;
        state.latestRecordedSpeed = 0.0f;
        report.result = wasSkipping ? RecordResult::SkippedIdle
                                    : RecordResult::StartedCropping;
        report.count = history.count;
        return report;
    }

    HistorySample sample{};
    sample.pose = input.pose;
    for (int i = 0; i < 3; ++i) {
        sample.engineVelocity[i] = std::isfinite(input.engineVelocity[i])
                                       ? input.engineVelocity[i]
                                       : 0.0f;
    }
    sample.recordedTick = input.tick;
    sample.serial = ++history.nextSerial;
    if (sample.serial == 0) sample.serial = ++history.nextSerial;
    sample.tickDelta = 1;
    sample.flags = input.admissible ? SampleAdmissible : std::uint8_t{0};
    if (input.climbing) sample.flags |= SampleClimb;
    sample.animFrame = input.animFrame;
    sample.animRate = input.animRate;
    sample.animKind = input.animKind;
    sample.animSlot = input.animSlot;
    sample.stickX = input.stickX;
    sample.stickY = input.stickY;

    if (state.havePreviousRecorded) {
        if (decision == RecordDecision::ResumeAfterCrop) {
            sample.tickDelta = 1;
            report.skippedTicks = input.tick > state.idleSkipStartTick
                                      ? input.tick - state.idleSkipStartTick
                                      : 0;
        } else {
            sample.tickDelta =
                clampTickDelta(input.tick - state.previousRecordedTick);
        }
    }

    if (state.haveDistinct) {
        const float step =
            distance3(input.pose.position, state.lastDistinctPosition);
        const std::uint64_t since = input.tick - state.lastDistinctTick;
        if (std::isfinite(step) && step >= kDistinctStepMeters) {
            sample.pathSpeed = recordedPathSpeed(step, clampTickDelta(since));
            state.lastDistinctPosition[0] = input.pose.position.x;
            state.lastDistinctPosition[1] = input.pose.position.y;
            state.lastDistinctPosition[2] = input.pose.position.z;
            state.lastDistinctTick = input.tick;
            state.lastDistinctSpeed = sample.pathSpeed;
        } else {
            sample.pathSpeed = carriedPathSpeed(state.lastDistinctSpeed, since);
        }
    } else {
        state.lastDistinctPosition[0] = input.pose.position.x;
        state.lastDistinctPosition[1] = input.pose.position.y;
        state.lastDistinctPosition[2] = input.pose.position.z;
        state.lastDistinctTick = input.tick;
        state.lastDistinctSpeed = 0.0f;
        state.haveDistinct = true;
    }

    history.samples[history.head] = sample;
    history.head = static_cast<std::uint16_t>((history.head + 1) %
                                              kHistoryCapacity);
    if (history.count < kHistoryCapacity) {
        ++history.count;
        report.reachedMinimum = history.count == kMinHistory;
        report.reachedCapacity = history.count == kHistoryCapacity;
    }

    state.previousRecorded = input.pose;
    state.previousRecordedTick = input.tick;
    state.havePreviousRecorded = true;
    state.latestRecordedSpeed = sample.pathSpeed;

    report.result = decision == RecordDecision::ResumeAfterCrop
                        ? RecordResult::ResumedAfterCrop
                        : RecordResult::Recorded;
    report.count = history.count;
    report.tickDelta = sample.tickDelta;
    report.pathSpeed = sample.pathSpeed;
    return report;
}

struct RouteStats {
    float seconds = 0.0f;
    float meters = 0.0f;
    float peakSpeed = 0.0f;
};

inline RouteStats routeStats(const RouteHistory& history) {
    RouteStats stats{};
    if (history.count == 0) return stats;
    std::uint16_t index = static_cast<std::uint16_t>(
        (history.head + kHistoryCapacity - history.count) % kHistoryCapacity);
    for (std::uint16_t i = 0; i < history.count; ++i) {
        const HistorySample& sample = history.samples[index];
        stats.seconds +=
            static_cast<float>(sample.tickDelta) / kNominalSampleRate;
        stats.meters += sample.pathSpeed *
                        static_cast<float>(sample.tickDelta) /
                        kNominalSampleRate;
        if (sample.pathSpeed > stats.peakSpeed) stats.peakSpeed = sample.pathSpeed;
        index = static_cast<std::uint16_t>((index + 1) % kHistoryCapacity);
    }
    return stats;
}

inline std::uint16_t reachableAdmissiblePrefix(const RouteHistory& history) {
    std::uint16_t reachable = 0;
    std::uint16_t index = previous(history.head, kHistoryCapacity);
    for (std::uint16_t i = 0; i < history.count; ++i) {
        if (!(history.samples[index].flags & SampleAdmissible)) break;
        ++reachable;
        index = previous(index, kHistoryCapacity);
    }
    return reachable;
}

struct AnimationHistogram {
    unsigned counts[7]{};
};

inline AnimationHistogram animationHistogram(const RouteHistory& history) {
    AnimationHistogram histogram{};
    std::uint16_t index = static_cast<std::uint16_t>(
        (history.head + kHistoryCapacity - history.count) % kHistoryCapacity);
    for (std::uint16_t i = 0; i < history.count; ++i) {
        const std::uint8_t kind = history.samples[index].animKind;
        if (kind < 7) ++histogram.counts[kind];
        index = static_cast<std::uint16_t>((index + 1) % kHistoryCapacity);
    }
    return histogram;
}

}  // namespace self_recall::pure
