#pragma once

#include "RecallPoseHistory.hpp"

namespace self_recall::pure {

enum class AnimationVisibility : std::uint8_t { Hidden, Visible, Invalid };

inline AnimationVisibility animationShapeVisibility(const RecordedPoseFrame& frame,
        unsigned modelIndex, unsigned boneIndex, unsigned materialIndex) {
    if (frame.header.modelCount > kPoseModelLimit || modelIndex >= frame.header.modelCount ||
        frame.header.boneCount > kPoseBoneLimit || frame.header.materialCount > kPoseMaterialLimit)
        return AnimationVisibility::Invalid;
    const auto& model = frame.models[modelIndex];
    const auto& id = model.identity;
    if (boneIndex >= id.boneCount || materialIndex >= id.materialCount ||
        id.firstBone + id.boneCount > frame.header.boneCount ||
        id.firstMaterial + id.materialCount > frame.header.materialCount || model.queueAdmission > 1)
        return AnimationVisibility::Invalid;
    if (!model.queueAdmission) return AnimationVisibility::Hidden;
    return visibilityBit(frame.visible.bones, static_cast<std::uint16_t>(id.firstBone + boneIndex)) &&
           visibilityBit(frame.visible.materials, static_cast<std::uint16_t>(id.firstMaterial + materialIndex))
         ? AnimationVisibility::Visible : AnimationVisibility::Hidden;
}

} // namespace self_recall::pure
