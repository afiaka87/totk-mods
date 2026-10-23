// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace totk::render {

enum class PoseInputStatus : std::uint8_t {
    Ready, IdentityChanged, OriginChanged, MissingBoneMetadata, RequiresLocalAnimation,
    MissingShapeMetadata, RequiresShapeAnimation,
};

struct PoseInput {
    alignas(16) std::byte skeleton[0x48];
    std::byte skeletonPadding[8];
    alignas(16) std::byte model[0x90];
    PoseInput() = default;
    PoseInput(const PoseInput&) = delete;
    PoseInput& operator=(const PoseInput&) = delete;

    template <class LiveIdentity, class PoseIdentity, class Layout, class Admit>
    PoseInputStatus prepare(const LiveIdentity& live, const PoseIdentity& pose,
                             bool sameRenderSpace, const void* bones,
                             const Layout& layout, const Admit& admit) {
        if (!bones || live.unit != pose.unit || live.skeleton != pose.skeleton ||
            live.resource != pose.resource || live.boneCount != pose.boneCount ||
            live.materialCount != pose.materialCount)
            return PoseInputStatus::IdentityChanged;
        if (!sameRenderSpace) return PoseInputStatus::OriginChanged;
        const auto* originalSkeleton = reinterpret_cast<const std::byte*>(live.skeleton);
        const std::byte* metadata;
        std::memcpy(&metadata, originalSkeleton + 0x10, sizeof(metadata));
        if (!metadata || !admit(metadata, 0, std::size_t{live.boneCount} * 0x58))
            return PoseInputStatus::MissingBoneMetadata;
        for (unsigned i = 0; i < live.boneCount; ++i) {
            std::uint32_t flags;
            std::memcpy(&flags, metadata + i * 0x58 + 0x2C, sizeof(flags));
            if (flags & 0x70000u) return PoseInputStatus::RequiresLocalAnimation;
        }
        const auto* unit = reinterpret_cast<const std::byte*>(live.unit);
        std::uint16_t shapeCount;
        const std::byte* shapes;
        std::memcpy(&shapeCount, unit + layout.shapeCount, sizeof(shapeCount));
        std::memcpy(&shapes, unit + layout.shapeArray, sizeof(shapes));
        if (shapeCount && (!shapes || !admit(shapes, 0, std::size_t{shapeCount} * 0x70)))
            return PoseInputStatus::MissingShapeMetadata;
        for (unsigned i = 0; i < shapeCount; ++i) {
            const std::byte* resource;
            std::memcpy(&resource, shapes + i * 0x70, sizeof(resource));
            if (!resource || !admit(resource, 0x5C, 1)) return PoseInputStatus::MissingShapeMetadata;
            if (resource[0x5C] != std::byte{0}) return PoseInputStatus::RequiresShapeAnimation;
        }
        std::memcpy(skeleton, originalSkeleton, sizeof(skeleton));
        std::memcpy(skeleton + 0x20, &bones, sizeof(bones));
        std::memcpy(model, unit + layout.embeddedModel, sizeof(model));
        const void* privateSkeleton = skeleton;
        std::memcpy(model + 0x38, &privateSkeleton, sizeof(privateSkeleton));
        return PoseInputStatus::Ready;
    }
};

struct PoseBoundsInput {
    alignas(16) std::byte unit[0x360];
    PoseBoundsInput() = default;
    PoseBoundsInput(const PoseBoundsInput&) = delete;
    PoseBoundsInput& operator=(const PoseBoundsInput&) = delete;

    template <class Layout>
    void prepare(const void* liveUnit, const PoseInput& animation, const Layout& layout) {
        std::memcpy(unit, liveUnit, sizeof(unit));
        const void* skeleton = animation.skeleton;
        std::memcpy(unit + layout.skeleton, &skeleton, sizeof(skeleton));
    }
};

static_assert(sizeof(PoseInput) == 0xE0 && alignof(PoseInput) == 16);
static_assert(offsetof(PoseInput, model) == 0x50);
static_assert(sizeof(PoseBoundsInput) == 0x360 && alignof(PoseBoundsInput) == 16);

} // namespace totk::render
