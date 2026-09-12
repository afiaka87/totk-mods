#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <thread>
#include "RecallModelEngine.hpp"
#include "RecallRender.hpp"
#include "doctest.h"
#include <vector>

namespace self_recall_tests::test_recall_render_frames {
using namespace self_recall::pure;

TEST_CASE("wrist effects select the latched render epoch even before GPU buffer preparation") {
    auto recorded = std::make_unique<RecordedPoseFrame>();
    auto store = std::make_unique<RenderFrameStore>();
    recorded->header.key.serial = 10;
    recorded->header.key.generation = 5;
    recorded->header.modelCount = 1;
    recorded->header.haveWrist = true;
    const float first[12]{0, -1, 0, 123, 1, 0, 0, -45, 0, 0, 1, 67};
    std::memcpy(recorded->header.wristMatrix, first, sizeof(first));
    REQUIRE(store->begin(*recorded, 101) == RenderPrepareStatus::Ready);
    recorded->header.key.serial = 9;
    recorded->header.wristMatrix[3] = 120;
    REQUIRE(store->begin(*recorded, 102) == RenderPrepareStatus::Ready);
    RenderWristFrame wrist;
    REQUIRE(store->copyWrist(101, 5, wrist));
    CHECK(wrist.key.serial == 10);
    CHECK(wrist.epoch == 101);
    CHECK(std::memcmp(wrist.matrix, first, sizeof(first)) == 0);
    REQUIRE(store->copyWrist(102, 5, wrist));
    CHECK(wrist.key.serial == 9);
    CHECK(wrist.matrix[3] == 120);
    CHECK_FALSE(store->copyWrist(102, 6, wrist));
    CHECK_FALSE(store->copyWrist(103, 5, wrist));
    recorded->header.haveWrist = false;
    REQUIRE(store->begin(*recorded, 103) == RenderPrepareStatus::Ready);
    CHECK_FALSE(store->copyWrist(103, 5, wrist));
}

namespace {
struct FrameFixture {
    RecordedPoseFrame recorded{};
    RecordedModelPose current{};
    std::unique_ptr<RenderFrameStore> store = std::make_unique<RenderFrameStore>();
    FrameFixture() {
        recorded.header.key = {1, 1};
        recorded.header.modelCount = recorded.header.boneCount = 1;
        recorded.models[0].identity = {11, 22, 33, 0, 1, 0, 0};
        recorded.models[0].originRelative = 1;
        recorded.models[0].renderOrigin[0] = 1000;
        float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 3, 4, 0};
        std::memcpy(recorded.bones[0].words, identity, sizeof(identity));
        current = recorded.models[0];
        current.renderOrigin[0] = 990;
    }
};
}

TEST_CASE("render snapshots pair historical animation with the native coordinate origin and buffer") {
    FrameFixture fixture;
    REQUIRE(fixture.store->begin(fixture.recorded, 1) == RenderPrepareStatus::Ready);
    auto old = fixture.store->acquireEpoch(11, 1, 1);
    REQUIRE(old.lease);
    REQUIRE(old.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    fixture.recorded.header.key.serial = 2;
    fixture.current.visibility = 1u << 24;
    REQUIRE(fixture.store->begin(fixture.recorded, 2) == RenderPrepareStatus::Ready);
    auto current = fixture.store->acquireEpoch(11, 2, 1);
    REQUIRE(current.lease);
    REQUIRE(current.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    CHECK(old.lease.get()->animation.header.key.serial == 1);
    CHECK(current.lease.get()->animation.header.key.serial == 2);
    CHECK(old.lease.get()->buffers[0] == 0);
    CHECK(current.lease.get()->buffers[0] == 1);
    float world[12];
    const auto& output = current.lease.get()->animation;
    REQUIRE(boneToWorldMatrix(output.bones[0], output.models[0], world));
    CHECK(world[3] == 1002);
    CHECK(world[7] == 3);
    CHECK(world[11] == 4);
    CHECK(output.models[0].renderOrigin[0] == 990);
    CHECK(fixture.recorded.models[0].renderOrigin[0] == 1000);
    CHECK_FALSE(fixture.store->acquireEpoch(99, 2, 1).owned);
    const auto late = fixture.store->acquireEpoch(11, 3, 1);
    CHECK(late.owned);
    CHECK_FALSE(late.lease);
    CHECK_FALSE(fixture.store->acquireEpoch(11, 2, 2).owned);
}

TEST_CASE("historical bounds completion belongs to one model epoch and cannot excuse a later missed upload") {
    FrameFixture fixture;
    REQUIRE(fixture.store->begin(fixture.recorded, 101) == RenderPrepareStatus::Ready);
    auto first = fixture.store->acquireEpoch(11, 101, 1);
    REQUIRE(first.lease);
    CHECK_FALSE(first.lease.bounded(0));
    first.lease.markBounded();
    CHECK(first.lease.bounded(0));
    CHECK_FALSE(first.lease.uploaded(0));
    REQUIRE(fixture.store->begin(fixture.recorded, 102) == RenderPrepareStatus::Ready);
    auto next = fixture.store->acquireEpoch(11, 102, 1);
    REQUIRE(next.lease);
    CHECK_FALSE(next.lease.bounded(0));
    CHECK_FALSE(next.lease.uploaded(0));
    CHECK(first.lease.bounded(0));
}

TEST_CASE("effect attachment matrices stay immutable across native origin rebasing") {
    FrameFixture fixture;
    REQUIRE(fixture.store->begin(fixture.recorded, 9) == RenderPrepareStatus::Ready);
    auto found = fixture.store->acquireEpoch(11, 9, 1);
    REQUIRE(found.lease);
    float before[12]{}, after[12]{};
    REQUIRE(found.lease.copyWorldBone(0, before));
    CHECK(before[3] == 1002);
    REQUIRE(found.lease.prepareBones(fixture.current) == RenderModelStatus::Ready);
    REQUIRE(found.lease.copyWorldBone(0, after));
    CHECK(std::memcmp(before, after, sizeof(before)) == 0);
    CHECK_FALSE(found.lease.copyWorldBone(1, after));
    CHECK_FALSE(found.lease.copyWorldBone(0, nullptr));
    fixture.recorded.models[0].identity.firstBone = kPoseBoneLimit;
    CHECK(fixture.store->begin(fixture.recorded, 10) == RenderPrepareStatus::InvalidFrame);
    REQUIRE(found.lease.copyWorldBone(0, after));
    CHECK(after[3] == 1002);
}

TEST_CASE("a native render lease prevents reuse until its upload finishes") {
    FrameFixture fixture;
    REQUIRE(fixture.store->begin(fixture.recorded, 1) == RenderPrepareStatus::Ready);
    auto held = fixture.store->acquireEpoch(11, 1, 1);
    REQUIRE(held.lease);
    fixture.recorded.header.key.serial = 2;
    REQUIRE(fixture.store->begin(fixture.recorded, 2) == RenderPrepareStatus::Ready);
    fixture.recorded.header.key.serial = 3;
    REQUIRE(fixture.store->begin(fixture.recorded, 3) == RenderPrepareStatus::Ready);
    fixture.recorded.header.key.serial = 4;
    CHECK(fixture.store->begin(fixture.recorded, 4) == RenderPrepareStatus::ReadersBusy);
    CHECK(held.lease.get()->animation.header.key.serial == 1);
    held.lease.release();
    CHECK(fixture.store->begin(fixture.recorded, 4) == RenderPrepareStatus::Ready);
    auto reused = fixture.store->acquireEpoch(11, 4, 1);
    REQUIRE(reused.lease);
    CHECK(reused.lease.get()->animation.header.key.serial == 4);
}

TEST_CASE("failed render preparation or model validation never exposes partial animation") {
    FrameFixture fixture;
    REQUIRE(fixture.store->begin(fixture.recorded, 1) == RenderPrepareStatus::Ready);
    auto frame = fixture.store->acquireEpoch(11, 1, 1);
    REQUIRE(frame.lease);
    fixture.current.identity.resource = 99;
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::ModelChanged);
    CHECK(frame.lease.get()->animation.models[0].identity.resource == 33);
    frame.lease.release();
    fixture.current.identity.resource = 33;
    fixture.recorded.bones[0].words[0] = 0x7FC00000u;
    CHECK(fixture.store->begin(fixture.recorded, 2) == RenderPrepareStatus::InvalidFrame);
    CHECK_FALSE(fixture.store->acquireEpoch(11, 2, 1).lease);
    fixture.recorded.bones[0].words[0] = 0x3f800000u;
    fixture.recorded.header.boneCount = kPoseBoneLimit + 1;
    CHECK(fixture.store->begin(fixture.recorded, 3) == RenderPrepareStatus::InvalidFrame);
}

TEST_CASE("one historical animation key is latched before native model uploads begin") {
    FrameFixture fixture;
    REQUIRE(fixture.store->begin(fixture.recorded, 17) == RenderPrepareStatus::Ready);
    fixture.recorded.header.key.serial = 99;
    auto frame = fixture.store->acquireEpoch(11, 17, 1);
    REQUIRE(frame.lease);
    CHECK(frame.lease.get()->animation.header.key.serial == 1);
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    const auto transformed = frame.lease.get()->animation.bones[0];
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    CHECK(std::memcmp(&transformed, &frame.lease.get()->animation.bones[0], sizeof(transformed)) == 0);
    CHECK(frame.lease.get()->buffers[0] == 0);
    fixture.current.visibility = 1u << 24;
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::BufferChanged);
    fixture.current.visibility = 0;
    fixture.current.renderOrigin[0] = 2000;
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::OriginChanged);
    CHECK_FALSE(fixture.store->acquireEpoch(11, 18, 1).lease);
}

TEST_CASE("parallel models rebase independent bone ranges in the same animation phase") {
    FrameFixture fixture;
    fixture.recorded.header.modelCount = fixture.recorded.header.boneCount = 2;
    fixture.recorded.models[1] = fixture.recorded.models[0];
    fixture.recorded.models[1].identity = {44, 55, 66, 1, 1, 0, 0};
    fixture.recorded.bones[1] = fixture.recorded.bones[0];
    auto second = fixture.recorded.models[1];
    second.renderOrigin[0] = 1010;
    second.visibility = 1u << 24;
    REQUIRE(fixture.store->begin(fixture.recorded, 18) == RenderPrepareStatus::Ready);
    std::array<RenderModelStatus, 2> status{};
    std::thread a([&] {
        auto frame = fixture.store->acquireEpoch(11, 18, 1);
        status[0] = frame.lease.prepareModel(fixture.current);
    });
    std::thread b([&] {
        auto frame = fixture.store->acquireEpoch(44, 18, 1);
        status[1] = frame.lease.prepareModel(second);
    });
    a.join(); b.join();
    CHECK(status[0] == RenderModelStatus::Ready);
    CHECK(status[1] == RenderModelStatus::Ready);
    auto frame = fixture.store->acquireEpoch(11, 18, 1);
    REQUIRE(frame.lease);
    const auto& output = frame.lease.get()->animation;
    for (unsigned i = 0; i < 2; ++i) {
        float world[12];
        REQUIRE(boneToWorldMatrix(output.bones[i], output.models[i], world));
        CHECK(world[3] == 1002);
    }
    CHECK(frame.lease.get()->buffers[0] == 0);
    CHECK(frame.lease.get()->buffers[1] == 1);
}

TEST_CASE("culling can prepare historical bones before the native GPU buffer advances") {
    FrameFixture fixture;
    REQUIRE(fixture.store->begin(fixture.recorded, 19) == RenderPrepareStatus::Ready);
    auto frame = fixture.store->acquireEpoch(11, 19, 1);
    REQUIRE(frame.lease);
    REQUIRE(frame.lease.prepareBones(fixture.current) == RenderModelStatus::Ready);
    CHECK_FALSE(frame.lease.uploaded(0));
    const auto boundsBones = frame.lease.get()->animation.bones[0];
    fixture.current.visibility = 2u << 24;
    REQUIRE(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    CHECK(frame.lease.get()->buffers[0] == 2);
    CHECK(std::memcmp(&boundsBones, &frame.lease.get()->animation.bones[0], sizeof(boundsBones)) == 0);
    CHECK(frame.lease.prepareBones(fixture.current) == RenderModelStatus::Ready);
    fixture.current.renderOrigin[0] += 1;
    CHECK(frame.lease.prepareBones(fixture.current) == RenderModelStatus::OriginChanged);
}

TEST_CASE("historical bounds do not pin an early native origin before GPU preparation") {
    using namespace self_recall::model;
    FrameFixture fixture;
    REQUIRE(fixture.store->begin(fixture.recorded, 20) == RenderPrepareStatus::Ready);
    auto frame = fixture.store->acquireEpoch(11, 20, 1);
    REQUIRE(frame.lease);
    alignas(16) std::array<std::byte, 0x400> unit{};
    unit[0x355] = std::byte{0xA5};
    std::memcpy(unit.data() + 0x338, fixture.current.renderOrigin, 12);
    const auto untouched = unit;
    NativeRenderInput input;
    NativeBoundingInput bounds;
    bounds.prepareHistorical(unit.data(), input, fixture.recorded.models[0]);
    RecordedModelPose boundsSpace{};
    boundsSpace.originRelative = std::to_integer<unsigned>(bounds.unit[0x355]) & 1u;
    std::memcpy(boundsSpace.renderOrigin, bounds.unit + 0x338, 12);
    float boundsWorld[12];
    REQUIRE(boneToWorldMatrix(fixture.recorded.bones[0], boundsSpace, boundsWorld));
    CHECK(boundsWorld[3] == 1002);
    CHECK(bounds.unit[0x355] == std::byte{0xA5});
    CHECK(unit == untouched);
    fixture.current.renderOrigin[0] = 1024;
    fixture.current.visibility = 1u << 24;
    REQUIRE(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    float renderWorld[12];
    const auto& render = frame.lease.get()->animation;
    REQUIRE(boneToWorldMatrix(render.bones[0], render.models[0], renderWorld));
    CHECK(renderWorld[3] == boundsWorld[3]);
    CHECK(frame.lease.get()->buffers[0] == 1);
    auto absolute = fixture.recorded.models[0];
    absolute.originRelative = 0;
    bounds.prepareHistorical(unit.data(), input, absolute);
    CHECK(bounds.unit[0x355] == std::byte{0xA4});
    CHECK(unit == untouched);
}

TEST_CASE("unused origins may change after upload while effective origin changes still fail") {
    FrameFixture fixture;
    fixture.current.originRelative = fixture.recorded.models[0].originRelative = 0;
    REQUIRE(fixture.store->begin(fixture.recorded, 21) == RenderPrepareStatus::Ready);
    auto frame = fixture.store->acquireEpoch(11, 21, 1);
    REQUIRE(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    frame.lease.markUploaded();
    const auto before = frame.lease.get()->animation.bones[0];
    fixture.current.renderOrigin[0] += 1024;
    REQUIRE(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    CHECK(frame.lease.uploaded(0));
    CHECK(std::memcmp(&before, &frame.lease.get()->animation.bones[0], sizeof(before)) == 0);
    fixture.current.originRelative = 1;
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::OriginChanged);
}

TEST_CASE("concurrent native uploads see complete immutable animation frames") {
    FrameFixture fixture;
    fixture.current.originRelative = fixture.recorded.models[0].originRelative = 0;
    std::atomic<bool> done{false};
    std::atomic<unsigned> errors{0};
    std::atomic<unsigned> reads{0};
    std::atomic<std::uint64_t> published{1};
    std::array<std::thread, 2> workers;
    fixture.recorded.bones[0].words[12] = 1;
    REQUIRE(fixture.store->begin(fixture.recorded, 1) == RenderPrepareStatus::Ready);
    for (auto& worker : workers) worker = std::thread([&] {
        while (!done.load(std::memory_order_acquire)) {
            const auto epoch = published.load(std::memory_order_acquire);
            auto frame = fixture.store->acquireEpoch(11, epoch, 1);
            if (!frame.lease) continue;
            const auto& animation = frame.lease.get()->animation;
            if (animation.header.key.serial != animation.bones[0].words[12]) ++errors;
            ++reads;
        }
    });
    while (!reads.load(std::memory_order_relaxed)) std::this_thread::yield();
    for (std::uint64_t i = 2; i <= 1000; ++i) {
        fixture.recorded.header.key.serial = i;
        fixture.recorded.bones[0].words[12] = static_cast<std::uint32_t>(i);
        while (fixture.store->begin(fixture.recorded, i) == RenderPrepareStatus::ReadersBusy)
            std::this_thread::yield();
        published.store(i, std::memory_order_release);
    }
    done.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    CHECK(errors.load() == 0);
    CHECK(reads.load() > 0);
}

TEST_CASE("private native render prefixes never replace pointers in the live model") {
    using namespace self_recall::model;
    alignas(16) std::array<std::byte, 0x400> unit{};
    alignas(16) std::array<std::byte, 0x48> skeleton{};
    alignas(16) std::array<std::byte, 0x58> metadata{};
    RecordedBoneMatrix bones[1]{};
    const void* metadataPointer = metadata.data();
    const void* skeletonPointer = skeleton.data();
    std::memcpy(skeleton.data() + 0x10, &metadataPointer, sizeof(metadataPointer));
    std::memcpy(unit.data() + 0x170, &skeletonPointer, sizeof(skeletonPointer));
    const auto beforeUnit = unit;
    const auto beforeSkeleton = skeleton;
    View view{};
    view.identity = {reinterpret_cast<std::uintptr_t>(unit.data()),
                     reinterpret_cast<std::uintptr_t>(skeleton.data()), 33, 1, 0};
    view.pose.identity = {view.identity.unit, view.identity.skeleton, 33, 0, 1, 0, 0};
    NativeRenderInput input;
    REQUIRE(input.prepare(view, view.pose, bones) == RenderInputStatus::Ready);
    const void* world;
    const void* shadow;
    std::memcpy(&world, input.skeleton + 0x20, sizeof(world));
    std::memcpy(&shadow, input.model + 0x38, sizeof(shadow));
    CHECK(world == bones);
    CHECK(shadow == input.skeleton);
    CHECK(unit == beforeUnit);
    CHECK(skeleton == beforeSkeleton);
    NativeBoundingInput bounding;
    bounding.prepare(unit.data(), input);
    const void* boundingSkeleton;
    std::memcpy(&boundingSkeleton, bounding.unit + 0x170, sizeof(boundingSkeleton));
    CHECK(boundingSkeleton == input.skeleton);
    CHECK(unit == beforeUnit);
    auto expectedPrefix = unit;
    const void* privateSkeleton = input.skeleton;
    std::memcpy(expectedPrefix.data() + 0x170, &privateSkeleton, sizeof(privateSkeleton));
    CHECK(std::memcmp(bounding.unit, expectedPrefix.data(), sizeof(bounding.unit)) == 0);
    auto changed = view.pose;
    changed.renderOrigin[0] = 1;
    CHECK(input.prepare(view, changed, bones) == RenderInputStatus::Ready);
    view.pose.originRelative = changed.originRelative = 1;
    CHECK(input.prepare(view, changed, bones) == RenderInputStatus::OriginChanged);
    view.pose.originRelative = 0;
    changed = view.pose;
    changed.identity.resource = 44;
    CHECK(input.prepare(view, changed, bones) == RenderInputStatus::IdentityChanged);
    const std::uint32_t billboard = 0x20000;
    std::memcpy(metadata.data() + 0x2C, &billboard, sizeof(billboard));
    CHECK(input.prepare(view, view.pose, bones) == RenderInputStatus::RequiresLocalAnimation);
    metadata.fill(std::byte{0});
    std::array<std::byte, 0x70> shape{};
    std::array<std::byte, 0x60> resource{};
    const void* shapePointer = shape.data();
    const void* resourcePointer = resource.data();
    const std::uint16_t shapeCount = 1;
    std::memcpy(shape.data(), &resourcePointer, sizeof(resourcePointer));
    std::memcpy(unit.data() + 0x168, &shapeCount, sizeof(shapeCount));
    std::memcpy(unit.data() + 0x178, &shapePointer, sizeof(shapePointer));
    CHECK(input.prepare(view, view.pose, bones) == RenderInputStatus::Ready);
    resource[0x5C] = std::byte{1};
    CHECK(input.prepare(view, view.pose, bones) == RenderInputStatus::RequiresShapeAnimation);
}

TEST_CASE("fractional root displacement moves bones equipment wrist and bounds together") {
    FrameFixture fixture;
    fixture.recorded.header.haveWrist = true;
    fixture.recorded.header.route.pose.position = {1002, 300, -40};
    float originalBone[12];
    REQUIRE(boneToWorldMatrix(fixture.recorded.bones[0], fixture.recorded.models[0], originalBone));
    std::memcpy(fixture.recorded.header.wristMatrix, originalBone, sizeof(originalBone));
    const auto untouched = fixture.recorded;
    const totk::core::WorldPosition offset{-1.75f, 4.5f, 0.25f};
    REQUIRE(fixture.store->begin(fixture.recorded, 22, offset) == RenderPrepareStatus::Ready);
    auto frame = fixture.store->acquireEpoch(11, 22, 1);
    REQUIRE(frame.lease);
    float effect[12];
    REQUIRE(frame.lease.copyWorldBone(0, effect));
    RenderWristFrame wrist;
    REQUIRE(fixture.store->copyWrist(22, 1, wrist));
    CHECK(std::memcmp(wrist.matrix, effect, sizeof(effect)) == 0);
    auto boundsSpace = translatedBoundsSpace(fixture.recorded.models[0], offset);
    float bound[12];
    REQUIRE(boneToWorldMatrix(fixture.recorded.bones[0], boundsSpace, bound));
    CHECK(std::memcmp(bound, effect, sizeof(bound)) == 0);
    REQUIRE(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    const auto& shown = frame.lease.get()->animation;
    float rendered[12];
    REQUIRE(boneToWorldMatrix(shown.bones[0], shown.models[0], rendered));
    CHECK(std::memcmp(rendered, effect, sizeof(rendered)) == 0);
    CHECK(shown.header.route.pose.position.x == 1000.25f);
    CHECK(shown.header.route.pose.position.y == 304.5f);
    for (unsigned i = 0; i < 12; ++i)
        if (i % 4 != 3) CHECK(rendered[i] == originalBone[i]);
    CHECK(std::memcmp(&fixture.recorded, &untouched, sizeof(untouched)) == 0);
    CHECK(shown.bones[0].words[15] == untouched.bones[0].words[15]);
}

TEST_CASE("draw validates the uploaded buffer after the CPU origin advances to the next preparation") {
    FrameFixture fixture;
    REQUIRE(fixture.store->begin(fixture.recorded, 23) == RenderPrepareStatus::Ready);
    auto frame = fixture.store->acquireEpoch(11, 23, 1);
    REQUIRE(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    CHECK(frame.lease.validateUploadedModel(fixture.current) == RenderModelStatus::Busy);
    frame.lease.markUploaded();
    const auto before = frame.lease.get()->animation.bones[0];
    fixture.current.renderOrigin[2] += 10;
    CHECK(frame.lease.validateUploadedModel(fixture.current) == RenderModelStatus::Ready);
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::OriginChanged);
    CHECK(std::memcmp(&before, &frame.lease.get()->animation.bones[0], sizeof(before)) == 0);
    fixture.current.visibility = 1u << 24;
    CHECK(frame.lease.validateUploadedModel(fixture.current) == RenderModelStatus::BufferChanged);
    fixture.current.identity.resource += 1;
    CHECK(frame.lease.validateUploadedModel(fixture.current) == RenderModelStatus::ModelChanged);
}
}

namespace self_recall_tests::test_recall_animation_visibility {
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
}

namespace self_recall_tests::test_recall_wrist_provider {
using namespace self_recall::pure;

TEST_CASE("presentation seals fractional translation with the animation key") {
    PresentationFrameLatch<PosePresentation> latch;
    PosePresentation first{{9, 1, 0}, {-1.75f, 4.5f, 0.25f}};
    REQUIRE(latch.publish(1, 10, first));
    PosePresentation queued = first;
    queued.offset.y = 30;
    CHECK(latch.publish(1, 10, queued).offset.y == 4.5f);
    CHECK(latch.snapshot(1).offset.x == -1.75f);
    CHECK(latch.snapshot(1).key == first.key);
    CHECK(latch.publish(1, 11, queued).offset.y == 30);
    CHECK_FALSE(latch.snapshot(2));
}

TEST_CASE("native effect-local transform survives late wrist translation rotation and scale") {
    const float wrist[12]{0,-2,0,100, 3,0,0,200, 0,0,4,-50};
    const float effect[12]{2,0,0,99.6f, 0,3,0,199.85f, 0,0,2,-48.4f};
    float local[12];
    REQUIRE(relativeEffectMatrix(wrist, effect, local));
    const float current[12]{1,0,0,200, 0,1,0,-100, 0,0,1,20};
    const float origin[3]{10,20,30};
    float columns[16];
    REQUIRE(emitterMatrix(current, local, origin, columns));
    const float expected[16]{0,-1,0,0, 1,0,0,0, 0,0,0.5f,0, 209.95f,-79.8f,50.4f,0};
    for (unsigned i = 0; i < 16; ++i) CHECK(columns[i] == doctest::Approx(expected[i]).epsilon(0.0001));
    float singular[12]{};
    CHECK_FALSE(relativeEffectMatrix(singular, effect, local));
    singular[0] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(relativeEffectMatrix(singular, effect, local));
}

TEST_CASE("emitter bindings reject recycled native sets and survive finite capacity teardown") {
    WristEmitterFrames<2> frames;
    WristEmitterFrame first{0x1000, 42, {1, 7}, {}};
    REQUIRE(frames.put(first));
    CHECK(frames.get(0x1000, 42).owner.serial == 1);
    CHECK_FALSE(frames.get(0x1000, 43).owner);
    first.instance = 43;
    first.owner = {2, 8};
    REQUIRE(frames.put(first));
    CHECK_FALSE(frames.get(0x1000, 42).owner);
    CHECK(frames.get(0x1000, 43).owner.serial == 2);
    first.emitterSet = 0x2000;
    REQUIRE(frames.put(first));
    first.emitterSet = 0x3000;
    CHECK_FALSE(frames.put(first));
    frames.clear();
    CHECK_FALSE(frames.get(0x1000, 43).owner);
    CHECK(frames.put(first));
}

TEST_CASE("only the native frame publisher can advance controller body and wrist presentation") {
    PresentationFrameLatch latch;
    const PoseFrameKey first{101, 4, 2}, advanced{100, 4, 1};
    CHECK_FALSE(latch.snapshot(1));
    CHECK(latch.publish(1, 20, first) == first);
    for (int reader = 0; reader < 3; ++reader) CHECK(latch.snapshot(1) == first);
    CHECK(latch.publish(1, 20, advanced) == first);
    CHECK(latch.publish(1, 21, advanced) == advanced);
    CHECK_FALSE(latch.publish(1, 20, first));
    CHECK(latch.snapshot(1) == advanced);
    CHECK(latch.publish(2, 21, first) == first);
    CHECK_FALSE(latch.publish(1, 22, advanced));
    CHECK_FALSE(latch.snapshot(1));
}

TEST_CASE("simultaneous presentation consumers cannot choose different frames") {
    PresentationFrameLatch latch;
    const PoseFrameKey expected{100, 4, 2};
    REQUIRE(latch.publish(1, 10, expected) == expected);
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    PoseFrameKey a, b;
    const auto consume = [&](PoseFrameKey& out) {
        ++ready;
        while (!start.load()) {}
        out = latch.snapshot(1);
    };
    std::thread first(consume, std::ref(a));
    std::thread second(consume, std::ref(b));
    while (ready.load() != 2) {}
    start.store(true);
    first.join();
    second.join();
    REQUIRE(a);
    CHECK(a == b);
    CHECK(a == expected);
}

TEST_CASE("accelerated presentation cadence does not depend on late consumer order") {
    for (const unsigned quarters : {7u, 8u, 16u}) {
        PresentationFrameLatch latch;
        for (unsigned clock = 1; clock <= 60; ++clock) {
            const PoseFrameKey presented{1000 - clock * quarters / 4, 1, 0};
            REQUIRE(latch.publish(1, clock, presented) == presented);
            const PoseFrameKey queued{1000 - (clock + 1) * quarters / 4, 1, 0};
            CHECK(latch.snapshot(1) == presented);
            CHECK(latch.publish(1, clock, queued) == presented);
            CHECK(latch.snapshot(1) == presented);
        }
    }
}

namespace {
const WristEffectBinding kStart{{0, 0, 7, 41}, 0x10000};
const WristEffectBinding kLoop{{0, 0, 9, 42}, 0x20000};

struct NativeSlots {
    bool (*valid)(const WristMatrixProvider*, const void*);
    void (*copy)(WristMatrixProvider*, void*, const void*);
};

NativeSlots slots(const WristMatrixDescriptor& descriptor) {
    const void* vtable = nullptr;
    std::memcpy(&vtable, descriptor.object, sizeof(vtable));
    NativeSlots out{};
    std::memcpy(&out, vtable, sizeof(out));
    return out;
}
}

TEST_CASE("wrist ownership rejects foreign events and recycled native event slots") {
    WristEffectOwners owners;
    const WristEffectBinding bindings[]{kStart, kLoop};
    const auto serial = owners.publish(11, bindings);
    REQUIRE(serial != 0);
    auto matched = owners.match(kStart.event, kStart.handle.eventId);
    REQUIRE(matched);
    CHECK(matched.serial == serial);
    CHECK(matched.historyGeneration == 11);
    CHECK(owners.match(kLoop.event, kLoop.handle.eventId));
    CHECK_FALSE(owners.match(kStart.event, kLoop.handle.eventId));
    CHECK_FALSE(owners.match(0x30000, kStart.handle.eventId));
    owners.clear();
    CHECK_FALSE(owners.match(kStart.event, kStart.handle.eventId));
    CHECK_FALSE(owners.current(serial));
    auto recycled = kStart;
    ++recycled.handle.eventId;
    const auto next = owners.publish(11, {&recycled, 1});
    CHECK(next > serial);
    CHECK_FALSE(owners.current(serial));
    CHECK_FALSE(owners.match(kStart.event, kStart.handle.eventId));
    CHECK(owners.match(recycled.event, recycled.handle.eventId));
    CHECK_FALSE(owners.match(kLoop.event, kLoop.handle.eventId));
}

TEST_CASE("native wrist provider preserves all recorded rotation and translation values") {
    WristEffectOwners owners;
    const auto serial = owners.publish(7, {&kStart, 1});
    const float matrix[12]{0, -1, 0, 123, 1, 0, 0, -45, 0, 0, 1, 67};
    WristMatrixProvider provider(owners, serial, matrix);
    const auto descriptor = provider.descriptor();
    const auto native = slots(descriptor);
    REQUIRE(native.valid(&provider, &descriptor.serial));
    float out[12]{};
    native.copy(&provider, out, &descriptor.serial);
    CHECK(provider.copied());
    CHECK(std::memcmp(out, matrix, sizeof(out)) == 0);
    auto wrongSerial = descriptor.serial + 1;
    CHECK_FALSE(native.valid(&provider, &wrongSerial));
}

TEST_CASE("effect teardown between native validation and copy leaves the native base intact") {
    WristEffectOwners owners;
    const auto serial = owners.publish(7, {&kStart, 1});
    const float matrix[12]{1, 0, 0, 20, 0, 1, 0, 30, 0, 0, 1, 40};
    WristMatrixProvider provider(owners, serial, matrix);
    const auto descriptor = provider.descriptor();
    const auto native = slots(descriptor);
    REQUIRE(native.valid(&provider, &descriptor.serial));
    owners.clear();
    float out[12]{5, 4, 3, 2};
    const auto before = std::bit_cast<std::array<float, 12>>(out);
    native.copy(&provider, out, &descriptor.serial);
    CHECK_FALSE(provider.copied());
    CHECK(std::memcmp(out, before.data(), sizeof(out)) == 0);
    CHECK_FALSE(native.valid(&provider, &descriptor.serial));
    (void)owners.publish(7, {&kStart, 1});
    CHECK_FALSE(native.valid(&provider, &descriptor.serial));
}

TEST_CASE("invalid wrist transforms and malformed bindings cannot override native effects") {
    WristEffectOwners owners;
    auto invalid = kStart;
    invalid.handle.poolIndex = -1;
    (void)owners.publish(7, {&invalid, 1});
    CHECK_FALSE(owners.match(invalid.event, invalid.handle.eventId));
    invalid = kStart;
    invalid.handle.type = 0xFF;
    (void)owners.publish(7, {&invalid, 1});
    CHECK_FALSE(owners.match(invalid.event, invalid.handle.eventId));
    const auto serial = owners.publish(7, {&kStart, 1});
    float matrix[12]{};
    matrix[11] = std::numeric_limits<float>::quiet_NaN();
    WristMatrixProvider provider(owners, serial, matrix);
    const auto descriptor = provider.descriptor();
    CHECK_FALSE(slots(descriptor).valid(&provider, &descriptor.serial));
    CHECK(owners.publish(0, {&kStart, 1}) == 0);
    CHECK_FALSE(owners.current(serial));
}

TEST_CASE("parallel wrist readers never combine owner generations during publication") {
    WristEffectOwners owners;
    std::atomic<bool> done{false};
    std::atomic<bool> mixed{false};
    std::atomic<unsigned> reads{0};
    (void)owners.publish(11, {&kStart, 1});
    std::thread reader([&] {
        do {
            const auto start = owners.match(kStart.event, kStart.handle.eventId);
            const auto loop = owners.match(kLoop.event, kLoop.handle.eventId);
            if ((start && start.historyGeneration != 11) || (loop && loop.historyGeneration != 22))
                mixed.store(true);
            reads.fetch_add(1);
        } while (!done.load());
    });
    for (unsigned i = 0; i < 4000; ++i) {
        (void)owners.publish(22, {&kLoop, 1});
        (void)owners.publish(11, {&kStart, 1});
    }
    done.store(true);
    reader.join();
    CHECK(reads.load() > 0);
    CHECK_FALSE(mixed.load());
}
}
