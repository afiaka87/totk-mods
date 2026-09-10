#pragma once

#include <cstring>
#include "RecallAnimationVisibility.hpp"

namespace self_recall::model {

inline pure::AnimationVisibility nativeShapeVisibility(const void* nativeUnit, unsigned shapeIndex,
        const pure::RecordedPoseFrame& frame, unsigned modelIndex) {
    if (!nativeUnit || frame.header.modelCount > pure::kPoseModelLimit ||
        modelIndex >= frame.header.modelCount) return pure::AnimationVisibility::Invalid;
    const auto& expected = frame.models[modelIndex].identity;
    if (expected.unit != reinterpret_cast<std::uintptr_t>(nativeUnit))
        return pure::AnimationVisibility::Invalid;
    const auto read = []<class T>(const void* base, std::size_t offset, T& out) {
        std::memcpy(&out, static_cast<const std::byte*>(base) + offset, sizeof(T));
    };
    const void* skeleton;
    read(nativeUnit, 0x170, skeleton);
    if (!skeleton || expected.skeleton != reinterpret_cast<std::uintptr_t>(skeleton))
        return pure::AnimationVisibility::Invalid;
    const void* resource;
    read(skeleton, 0, resource);
    if (!resource || expected.resource != reinterpret_cast<std::uintptr_t>(resource))
        return pure::AnimationVisibility::Invalid;
    std::uint16_t bones, materials, shapes;
    read(resource, 0x38, bones);
    read(nativeUnit, 0x16A, materials);
    read(nativeUnit, 0x168, shapes);
    if (bones != expected.boneCount || materials != expected.materialCount || shapeIndex >= shapes)
        return pure::AnimationVisibility::Invalid;
    const std::byte* shapeArray;
    read(nativeUnit, 0x178, shapeArray);
    if (!shapeArray) return pure::AnimationVisibility::Invalid;
    const void* shape;
    read(shapeArray, shapeIndex * 0x70, shape);
    if (!shape) return pure::AnimationVisibility::Invalid;
    std::uint16_t material, bone;
    read(shape, 0x52, material);
    read(shape, 0x54, bone);
    return pure::animationShapeVisibility(frame, modelIndex, bone, material);
}

inline pure::AnimationVisibility nativeModelVisibility(const void* unit,
        const pure::RecordedPoseFrame& frame, unsigned modelIndex) {
    if (!unit || modelIndex >= frame.header.modelCount ||
        frame.header.modelCount > pure::kPoseModelLimit ||
        frame.models[modelIndex].identity.unit != reinterpret_cast<std::uintptr_t>(unit))
        return pure::AnimationVisibility::Invalid;
    std::uint16_t shapes;
    std::memcpy(&shapes, static_cast<const std::byte*>(unit) + 0x168, sizeof(shapes));
    bool visible = false;
    for (unsigned i = 0; i < shapes; ++i) {
        const auto shape = nativeShapeVisibility(unit, i, frame, modelIndex);
        if (shape == pure::AnimationVisibility::Invalid) return shape;
        visible |= shape == pure::AnimationVisibility::Visible;
    }
    return visible ? pure::AnimationVisibility::Visible : pure::AnimationVisibility::Hidden;
}

} // namespace self_recall::model
