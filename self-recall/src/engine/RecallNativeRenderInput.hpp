#pragma once

#include <cstddef>
#include <cstring>
#include "RecallModelView.hpp"
#include "RecallPoseTransport.hpp"

namespace self_recall::model {

enum class RenderInputStatus : std::uint8_t {
    Ready, IdentityChanged, OriginChanged, MissingBoneMetadata, RequiresLocalAnimation,
    MissingShapeMetadata, RequiresShapeAnimation,
};

struct NativeRenderInput {
    alignas(16) std::byte skeleton[0x48];
    std::byte skeletonPadding[8];
    alignas(16) std::byte model[0x90];
    NativeRenderInput() = default;
    NativeRenderInput(const NativeRenderInput&) = delete;
    NativeRenderInput& operator=(const NativeRenderInput&) = delete;

    RenderInputStatus prepare(const View& live, const pure::RecordedModelPose& historical,
                               const pure::RecordedBoneMatrix* bones) {
        const auto& a = live.identity;
        const auto& b = historical.identity;
        if (!bones || a.unit != b.unit || a.skeleton != b.skeleton || a.resource != b.resource ||
            a.boneCount != b.boneCount || a.materialCount != b.materialCount)
            return RenderInputStatus::IdentityChanged;
        if (!pure::sameRenderSpace(live.pose, historical))
            return RenderInputStatus::OriginChanged;
        const auto* originalSkeleton = reinterpret_cast<const std::byte*>(a.skeleton);
        const std::byte* metadata;
        std::memcpy(&metadata, originalSkeleton + 0x10, sizeof(metadata));
        if (!metadata) return RenderInputStatus::MissingBoneMetadata;
        for (unsigned i = 0; i < a.boneCount; ++i) {
            std::uint32_t flags;
            std::memcpy(&flags, metadata + i * 0x58 + 0x2C, sizeof(flags));
            if (flags & 0x70000u) return RenderInputStatus::RequiresLocalAnimation;
        }
        const auto* unit = reinterpret_cast<const std::byte*>(a.unit);
        std::uint16_t shapeCount;
        const std::byte* shapes;
        std::memcpy(&shapeCount, unit + 0x168, sizeof(shapeCount));
        std::memcpy(&shapes, unit + 0x178, sizeof(shapes));
        if (shapeCount && !shapes) return RenderInputStatus::MissingShapeMetadata;
        for (unsigned i = 0; i < shapeCount; ++i) {
            const std::byte* resource;
            std::memcpy(&resource, shapes + i * 0x70, sizeof(resource));
            if (!resource) return RenderInputStatus::MissingShapeMetadata;
            if (resource[0x5C] != std::byte{0}) return RenderInputStatus::RequiresShapeAnimation;
        }
        std::memcpy(skeleton, originalSkeleton, sizeof(skeleton));
        std::memcpy(skeleton + 0x20, &bones, sizeof(bones));
        std::memcpy(model, unit + 0x138, sizeof(model));
        const void* privateSkeleton = skeleton;
        std::memcpy(model + 0x38, &privateSkeleton, sizeof(privateSkeleton));
        return RenderInputStatus::Ready;
    }
};

struct NativeBoundingInput {
    alignas(16) std::byte unit[0x360];
    NativeBoundingInput() = default;
    NativeBoundingInput(const NativeBoundingInput&) = delete;
    NativeBoundingInput& operator=(const NativeBoundingInput&) = delete;

    void prepare(const void* liveUnit, const NativeRenderInput& animation) {
        std::memcpy(unit, liveUnit, sizeof(unit));
        const void* skeleton = animation.skeleton;
        std::memcpy(unit + 0x170, &skeleton, sizeof(skeleton));
    }

    void prepareHistorical(const void* liveUnit, const NativeRenderInput& animation,
                           const pure::RecordedModelPose& historical) {
        prepare(liveUnit, animation);
        std::memcpy(unit + 0x338, historical.renderOrigin, sizeof(historical.renderOrigin));
        const auto flags = std::to_integer<unsigned>(unit[0x355]);
        unit[0x355] = static_cast<std::byte>((flags & ~1u) | historical.originRelative);
    }
};

} // namespace self_recall::model
