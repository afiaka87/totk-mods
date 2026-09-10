#pragma once

#include <cstdint>
#include <span>

#include "RecallPoseHistory.hpp"

namespace self_recall::model {

struct Identity {
    std::uintptr_t unit = 0;
    std::uintptr_t skeleton = 0;
    std::uintptr_t resource = 0;
    std::uint16_t boneCount = 0;
    std::uint16_t materialCount = 0;
    bool operator==(const Identity&) const = default;
};

struct View {
    Identity identity{};
    pure::RecordedModelPose pose{};
    const void* boneBytes = nullptr;
    const std::uint32_t* boneVisibility = nullptr;
    const std::uint32_t* materialVisibility = nullptr;
    std::int32_t wristIndex = -1;
};

enum class ViewStatus : std::uint8_t {
    Ready,
    UnsupportedType,
    MissingSkeleton,
    BoneLimitExceeded,
    MaterialLimitExceeded,
    MissingVisibility,
};

ViewStatus describe(std::uintptr_t mainBase, const void* unit, View& out);

bool copyWrist(const View& view, float out[12]);

struct CaptureWorkspace {
    pure::RecordedModelPose models[pure::kPoseModelLimit]{};
    pure::RecordedBoneMatrix bones[pure::kPoseBoneLimit]{};
};

enum class CaptureStatus : std::uint8_t {
    Recorded,
    ModelCountMismatch,
    BoneRangeMismatch,
    MissingWrist,
    HistoryRejected,
    MaterialRangeMismatch,
    MissingVisibility,
};

struct CaptureReport {
    CaptureStatus status = CaptureStatus::HistoryRejected;
    pure::PoseRecordReport history{};
};

CaptureReport recordCompleted(const pure::PoseFrameHeader& header,
                              std::span<const View> views,
                              CaptureWorkspace& workspace,
                              pure::PoseHistory& history);

}  // namespace self_recall::model
