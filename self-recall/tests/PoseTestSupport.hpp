#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "RecallPoseData.hpp"

namespace self_recall_tests {

struct PoseTestInput {
    std::vector<self_recall::pure::RecordedModelPose> models;
    std::vector<self_recall::pure::RecordedBoneMatrix> bones;
    self_recall::pure::PoseFrameInput value{};

    PoseTestInput(unsigned modelCount = 1, unsigned boneCount = 1,
                  unsigned materialCount = 0)
        : models(modelCount), bones(boneCount) {
        using namespace self_recall::pure;
        value.models = models.data();
        value.bones = bones.data();
        value.header.modelCount = static_cast<std::uint16_t>(modelCount);
        value.header.boneCount = static_cast<std::uint16_t>(boneCount);
        value.header.materialCount = static_cast<std::uint16_t>(materialCount);
        value.header.worldGeneration = value.header.modelGeneration = 1;
        for (unsigned i = 0; i < modelCount; ++i) {
            const bool last = i + 1 == modelCount;
            const auto materialBegin = materialCount ? i : 0;
            const auto materialSpan = materialCount ? (last ? materialCount - i : 1) : 0;
            models[i].identity = {
                i + 1u,
                i + 101u,
                i + 201u,
                static_cast<std::uint16_t>(i),
                static_cast<std::uint16_t>(last ? boneCount - i : 1),
                static_cast<std::uint16_t>(materialBegin),
                static_cast<std::uint16_t>(materialSpan),
            };
        }
        at(1);
    }

    void at(unsigned epoch, unsigned fps = 60) {
        value.header.frameEpoch = epoch;
        value.header.elapsedNanoseconds = std::uint64_t{epoch} * 1'000'000'000 / fps;
        value.header.route.pose.position.x = static_cast<float>(epoch);
    }
};

struct PoseTestStorage {
    std::unique_ptr<self_recall::pure::PoseHistorySlot[]> slots;
    std::unique_ptr<self_recall::pure::PosePayloadBlock[]> blocks;
    self_recall::pure::PoseHistory history;

    PoseTestStorage(unsigned slotCount, unsigned blockCount)
        : slots(std::make_unique<self_recall::pure::PoseHistorySlot[]>(slotCount)),
          blocks(std::make_unique<self_recall::pure::PosePayloadBlock[]>(blockCount)),
          history(slots.get(), slotCount, std::span{blocks.get(), blockCount}) {}
};

}
