#include <array>
#include <memory>
#include <vector>

#include "RecallNativeShapeVisibility.hpp"
#include "RecallRenderFrames.hpp"
#include "RecallArchiveLifecycle.hpp"
#include "doctest.h"

using namespace self_recall::pure;

TEST_CASE("archived model construction retains native bounding shapes before render hiding") {
    const std::array<bool, 3> nativeVisible{true, false, true};
    const auto initializeBounds = [&](bool hide) {
        std::vector<unsigned> bounds;
        for (unsigned shape = 0; shape < nativeVisible.size(); ++shape)
            if (!hide && nativeVisible[shape]) bounds.push_back(shape);
        return bounds;
    };
    CHECK(initializeBounds(true).empty());
    const auto bounds = initializeBounds(archiveSuppressesUnselectedShapes(ArchiveLife::Constructing));
    REQUIRE(bounds == std::vector<unsigned>{0, 2});
    for (const auto life : {ArchiveLife::Ready, ArchiveLife::Retiring}) {
        CHECK(archiveSuppressesUnselectedShapes(life));
        CHECK(bounds == std::vector<unsigned>{0, 2});
    }
    CHECK_FALSE(archiveSuppressesUnselectedShapes(ArchiveLife::Empty));
    CHECK_FALSE(archiveSuppressesUnselectedShapes(ArchiveLife::Destroyed));
}

TEST_CASE("historical shape visibility combines admission with independent bone and material bits") {
    auto frame = std::make_unique<RecordedPoseFrame>();
    frame->header.modelCount = 2;
    frame->header.boneCount = 33;
    frame->header.materialCount = 65;
    frame->models[0].identity = {11, 22, 33, 0, 31, 0, 63};
    frame->models[1].identity = {44, 55, 66, 31, 2, 63, 2};
    frame->models[1].queueAdmission = 1;
    for (unsigned bone = 0; bone != 2; ++bone) {
        for (unsigned material = 0; material != 2; ++material) {
            frame->visible.bones[1] = bone;
            frame->visible.materials[2] = material;
            CHECK(animationShapeVisibility(*frame, 1, 1, 1) ==
                (bone && material ? AnimationVisibility::Visible : AnimationVisibility::Hidden));
        }
    }
    CHECK(animationShapeVisibility(*frame, 1, 0, 1) == AnimationVisibility::Hidden);
    CHECK(animationShapeVisibility(*frame, 1, 1, 0) == AnimationVisibility::Hidden);
    frame->visible.bones[0] = 1u << 31;
    frame->visible.materials[1] = 1u << 31;
    CHECK(animationShapeVisibility(*frame, 1, 0, 0) == AnimationVisibility::Visible);
    frame->models[1].queueAdmission = 0;
    CHECK(animationShapeVisibility(*frame, 1, 1, 1) == AnimationVisibility::Hidden);
    frame->models[1].queueAdmission = 2;
    CHECK(animationShapeVisibility(*frame, 1, 1, 1) == AnimationVisibility::Invalid);
}

TEST_CASE("invalid shape and aggregate visibility ranges cannot read another model") {
    auto frame = std::make_unique<RecordedPoseFrame>();
    frame->header.modelCount = frame->header.boneCount = frame->header.materialCount = 1;
    frame->models[0].identity = {11, 22, 33, 0, 1, 0, 1};
    CHECK(animationShapeVisibility(*frame, 1, 0, 0) == AnimationVisibility::Invalid);
    CHECK(animationShapeVisibility(*frame, 0, 1, 0) == AnimationVisibility::Invalid);
    CHECK(animationShapeVisibility(*frame, 0, 0, 1) == AnimationVisibility::Invalid);
    frame->models[0].identity.firstBone = 1;
    CHECK(animationShapeVisibility(*frame, 0, 0, 0) == AnimationVisibility::Invalid);
    frame->models[0].identity.firstBone = 0;
    frame->models[0].identity.firstMaterial = 1;
    CHECK(animationShapeVisibility(*frame, 0, 0, 0) == AnimationVisibility::Invalid);
    frame->header.modelCount = kPoseModelLimit + 1;
    CHECK(animationShapeVisibility(*frame, 0, 0, 0) == AnimationVisibility::Invalid);
}

TEST_CASE("native shape lookup uses recorded visibility and verifies current skeleton identity") {
    using namespace self_recall::model;
    std::array<std::byte, 0x180> unit{};
    std::array<std::byte, 0x48> skeleton{};
    std::array<std::byte, 0x40> resource{};
    std::array<std::byte, 0xE0> shapes{};
    std::array<std::byte, 0x60> shape0{}, shape1{};
    const auto write = []<class T>(auto& destination, unsigned offset, T value) {
        std::memcpy(destination.data() + offset, &value, sizeof(value));
    };
    write(unit, 0x170, skeleton.data());
    write(unit, 0x178, shapes.data());
    write(unit, 0x168, std::uint16_t{2});
    write(unit, 0x16A, std::uint16_t{1});
    write(skeleton, 0, resource.data());
    write(resource, 0x38, std::uint16_t{2});
    write(shapes, 0, shape0.data());
    write(shapes, 0x70, shape1.data());
    write(shape1, 0x54, std::uint16_t{1});
    auto frame = std::make_unique<RecordedPoseFrame>();
    frame->header.modelCount = 1;
    frame->header.boneCount = 2;
    frame->header.materialCount = 1;
    frame->models[0].identity = {reinterpret_cast<std::uintptr_t>(unit.data()),
        reinterpret_cast<std::uintptr_t>(skeleton.data()), reinterpret_cast<std::uintptr_t>(resource.data()),
        0, 2, 0, 1};
    frame->models[0].queueAdmission = 1;
    frame->visible.bones[0] = 2;
    frame->visible.materials[0] = 1;
    const auto beforeUnit = unit;
    const auto beforeSkeleton = skeleton;
    CHECK(nativeShapeVisibility(unit.data(), 0, *frame, 0) == AnimationVisibility::Hidden);
    CHECK(nativeShapeVisibility(unit.data(), 1, *frame, 0) == AnimationVisibility::Visible);
    CHECK(nativeModelVisibility(unit.data(), *frame, 0) == AnimationVisibility::Visible);
    frame->visible.materials[0] = 0;
    CHECK(nativeModelVisibility(unit.data(), *frame, 0) == AnimationVisibility::Hidden);
    frame->visible.materials[0] = 1;
    frame->visible.bones[0] = 0;
    CHECK(nativeModelVisibility(unit.data(), *frame, 0) == AnimationVisibility::Hidden);
    frame->visible.bones[0] = 2;
    frame->models[0].queueAdmission = 0;
    CHECK(nativeModelVisibility(unit.data(), *frame, 0) == AnimationVisibility::Hidden);
    frame->models[0].queueAdmission = 1;
    CHECK(nativeShapeVisibility(unit.data(), 2, *frame, 0) == AnimationVisibility::Invalid);
    CHECK(unit == beforeUnit);
    CHECK(skeleton == beforeSkeleton);
    frame->models[0].identity.resource = 1;
    CHECK(nativeShapeVisibility(unit.data(), 1, *frame, 0) == AnimationVisibility::Invalid);
    CHECK(nativeModelVisibility(unit.data(), *frame, 0) == AnimationVisibility::Invalid);
    frame->models[0].identity.resource = reinterpret_cast<std::uintptr_t>(resource.data());
    write(shape1, 0x52, std::uint16_t{1});
    CHECK(nativeShapeVisibility(unit.data(), 1, *frame, 0) == AnimationVisibility::Invalid);
    frame->visible.bones[0] = 3;
    CHECK(nativeModelVisibility(unit.data(), *frame, 0) == AnimationVisibility::Invalid);
}

TEST_CASE("animation preparation cannot certify a GPU upload and completion expires each epoch") {
    auto frame = std::make_unique<RecordedPoseFrame>();
    auto store = std::make_unique<RenderFrameStore>();
    frame->header.key = {1, 1};
    frame->header.modelCount = frame->header.boneCount = 2;
    frame->models[0].identity = {11, 22, 33, 0, 1, 0, 0};
    frame->models[1].identity = {44, 55, 66, 1, 1, 0, 0};
    REQUIRE(store->begin(*frame, 1) == RenderPrepareStatus::Ready);
    auto first = store->acquireEpoch(11, 1, 1);
    REQUIRE(first.lease);
    first.lease.markUploaded();
    CHECK_FALSE(first.lease.uploaded(0));
    REQUIRE(first.lease.prepareModel(frame->models[0]) == RenderModelStatus::Ready);
    CHECK_FALSE(first.lease.uploaded(0));
    first.lease.markUploaded();
    CHECK(first.lease.uploaded(0));
    CHECK_FALSE(first.lease.uploaded(1));
    CHECK_FALSE(first.lease.uploaded(2));
    REQUIRE(store->begin(*frame, 2) == RenderPrepareStatus::Ready);
    auto second = store->acquireEpoch(11, 2, 1);
    REQUIRE(second.lease);
    CHECK_FALSE(second.lease.uploaded(0));
    CHECK(first.lease.uploaded(0));
    first.lease.release();
    REQUIRE(store->begin(*frame, 3) == RenderPrepareStatus::Ready);
    REQUIRE(store->begin(*frame, 4) == RenderPrepareStatus::Ready);
    CHECK_FALSE(store->acquireEpoch(11, 4, 1).lease.uploaded(0));
    frame->header.materialCount = kPoseMaterialLimit + 1;
    second.lease.release();
    CHECK(store->begin(*frame, 5) == RenderPrepareStatus::InvalidFrame);
}
