#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

#include "RecallHistory.hpp"

namespace self_recall::pure {

inline constexpr std::uint16_t kPoseModelLimit = 32;
inline constexpr std::uint16_t kPoseBoneLimit = 512;
inline constexpr std::uint16_t kPoseMaterialLimit = 512;
inline constexpr std::size_t kPoseHistoryByteLimit = 75u * 1024u * 1024u;

struct alignas(16) RecordedBoneMatrix {
    std::uint32_t words[16]{};
};
static_assert(sizeof(RecordedBoneMatrix) == 64);

struct RecordedModelIdentity {
    std::uint64_t unit = 0;
    std::uint64_t skeleton = 0;
    std::uint64_t resource = 0;
    std::uint16_t firstBone = 0;
    std::uint16_t boneCount = 0;
    std::uint16_t firstMaterial = 0;
    std::uint16_t materialCount = 0;
};
static_assert(sizeof(RecordedModelIdentity) == 32);

struct RecordedModelPose {
    float renderOrigin[3]{};
    std::uint32_t visibility = 0;
    std::uint32_t originRelative = 0;
    std::uint32_t queueAdmission = 0;
    RecordedModelIdentity identity{};
};
static_assert(sizeof(RecordedModelPose) == 56);

struct PoseFrameKey {
    std::uint64_t serial = 0;
    std::uint32_t generation = 0;
    std::uint32_t slot = 0;

    bool operator==(const PoseFrameKey&) const = default;
    explicit operator bool() const { return serial != 0 && generation != 0; }
};

struct PoseFrameHeader {
    PoseFrameKey key{};
    std::uint64_t frameEpoch = 0;
    std::uint64_t elapsedNanoseconds = 0;
    std::uint32_t worldGeneration = 0;
    std::uint32_t modelGeneration = 0;
    HistorySample route{};
    float wristMatrix[12]{};
    std::uint16_t modelCount = 0;
    std::uint16_t boneCount = 0;
    std::uint16_t materialCount = 0;
    bool haveWrist = false;
    std::uint8_t bodyModelCount = 0;
    std::uint8_t reserved[8]{}; // Existing equipment-effect mask; preserve verbatim.
    float waterHeight = 0;
    bool haveWaterHeight = false;
    std::uint8_t waterPadding[11]{};
};
static_assert(sizeof(PoseFrameHeader) % 16 == 0);

struct RecordedVisibility {
    std::uint32_t bones[kPoseBoneLimit / 32]{};
    std::uint32_t materials[kPoseMaterialLimit / 32]{};
};
static_assert(sizeof(RecordedVisibility) == 128);

inline bool visibilityBit(const std::uint32_t* bits, std::uint16_t index) {
    return (bits[index / 32] & (1u << (index % 32))) != 0;
}

struct RecordedPoseFrame {
    PoseFrameHeader header{};
    RecordedModelPose models[kPoseModelLimit]{};
    RecordedBoneMatrix bones[kPoseBoneLimit]{};
    RecordedVisibility visible{};
};

struct PoseFrameInput {
    PoseFrameHeader header{};  // key is assigned by the history
    const RecordedModelPose* models = nullptr;
    const RecordedBoneMatrix* bones = nullptr;
    RecordedVisibility visible{};
};


} // namespace self_recall::pure
