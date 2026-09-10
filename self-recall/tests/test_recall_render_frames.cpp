#include <array>
#include <atomic>
#include <memory>
#include <thread>

#include "RecallNativeRenderInput.hpp"
#include "RecallRenderFrames.hpp"
#include "doctest.h"

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
    RenderPrepareStatus publish(RenderFrameStore& store, std::uint64_t epoch, unsigned buffer) {
        current.visibility = buffer << 24;
        recorded.header.key.serial = epoch;
        return store.publish(recorded, {&current, 1}, epoch);
    }
};
}

TEST_CASE("render snapshots pair historical animation with the native coordinate origin and buffer") {
    FrameFixture fixture;
    auto store = std::make_unique<RenderFrameStore>();
    REQUIRE(fixture.publish(*store, 1, 0) == RenderPrepareStatus::Ready);
    REQUIRE(fixture.publish(*store, 2, 1) == RenderPrepareStatus::Ready);
    auto old = store->acquire(11, 0, 1);
    auto current = store->acquire(11, 1, 1);
    REQUIRE(old.lease);
    REQUIRE(current.lease);
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
    CHECK_FALSE(store->acquire(99, 0, 1).owned);
    CHECK(store->acquire(11, 2, 1).owned);
    CHECK_FALSE(store->acquire(11, 0, 2).owned);
}

TEST_CASE("historical bounds completion belongs to one model epoch and cannot excuse a later missed upload") {
    FrameFixture fixture;
    auto store = std::make_unique<RenderFrameStore>();
    REQUIRE(fixture.publish(*store, 101, 0) == RenderPrepareStatus::Ready);
    auto first = store->acquireEpoch(11, 101, 1);
    REQUIRE(first.lease);
    CHECK_FALSE(first.lease.bounded(0));
    first.lease.markBounded();
    CHECK(first.lease.bounded(0));
    CHECK_FALSE(first.lease.uploaded(0));
    REQUIRE(fixture.publish(*store, 102, 1) == RenderPrepareStatus::Ready);
    auto next = store->acquireEpoch(11, 102, 1);
    REQUIRE(next.lease);
    CHECK_FALSE(next.lease.bounded(0));
    CHECK_FALSE(next.lease.uploaded(0));
    CHECK(first.lease.bounded(0));
}

TEST_CASE("effect attachment matrices stay immutable across native origin rebasing") {
    FrameFixture fixture;
    auto store = std::make_unique<RenderFrameStore>();
    REQUIRE(store->begin(fixture.recorded, 9) == RenderPrepareStatus::Ready);
    auto found = store->acquireEpoch(11, 9, 1);
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
    CHECK(store->begin(fixture.recorded, 10) == RenderPrepareStatus::InvalidFrame);
    REQUIRE(found.lease.copyWorldBone(0, after));
    CHECK(after[3] == 1002);
}

TEST_CASE("a native render lease prevents reuse until its upload finishes") {
    FrameFixture fixture;
    auto store = std::make_unique<RenderFrameStore>();
    REQUIRE(fixture.publish(*store, 1, 0) == RenderPrepareStatus::Ready);
    auto held = store->acquire(11, 0, 1);
    REQUIRE(held.lease);
    REQUIRE(fixture.publish(*store, 2, 1) == RenderPrepareStatus::Ready);
    REQUIRE(fixture.publish(*store, 3, 2) == RenderPrepareStatus::Ready);
    CHECK(fixture.publish(*store, 4, 0) == RenderPrepareStatus::ReadersBusy);
    CHECK(held.lease.get()->animation.header.key.serial == 1);
    held.lease.release();
    CHECK(fixture.publish(*store, 4, 0) == RenderPrepareStatus::Ready);
    CHECK(store->acquire(11, 0, 1).lease.get()->animation.header.key.serial == 4);
}

TEST_CASE("failed render preparation never publishes a partial animation frame") {
    FrameFixture fixture;
    auto store = std::make_unique<RenderFrameStore>();
    fixture.current.identity.resource = 99;
    CHECK(fixture.publish(*store, 1, 0) == RenderPrepareStatus::ModelMismatch);
    CHECK_FALSE(store->acquire(11, 0, 1).lease);
    fixture.current.identity.resource = 33;
    fixture.recorded.bones[0].words[0] = 0x7FC00000u;
    CHECK(fixture.publish(*store, 2, 0) == RenderPrepareStatus::InvalidTransform);
    CHECK_FALSE(store->acquire(11, 0, 1).lease);
    fixture.recorded.header.boneCount = kPoseBoneLimit + 1;
    CHECK(fixture.publish(*store, 3, 0) == RenderPrepareStatus::InvalidFrame);
}

TEST_CASE("one historical animation key is latched before native model uploads begin") {
    FrameFixture fixture;
    auto store = std::make_unique<RenderFrameStore>();
    REQUIRE(store->begin(fixture.recorded, 17) == RenderPrepareStatus::Ready);
    fixture.recorded.header.key.serial = 99; // A later input selection cannot change this phase.
    auto frame = store->acquireEpoch(11, 17, 1);
    REQUIRE(frame.lease);
    CHECK(frame.lease.get()->animation.header.key.serial == 1);
    CHECK_FALSE(store->acquire(11, 0, 1).lease); // No native buffer has been assigned yet.
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    const auto transformed = frame.lease.get()->animation.bones[0];
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    CHECK(std::memcmp(&transformed, &frame.lease.get()->animation.bones[0], sizeof(transformed)) == 0);
    CHECK(store->acquire(11, 0, 1).lease);
    fixture.current.visibility = 1u << 24;
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::BufferChanged);
    fixture.current.visibility = 0;
    fixture.current.renderOrigin[0] = 2000;
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::OriginChanged);
    CHECK_FALSE(store->acquireEpoch(11, 18, 1).lease);
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
    auto store = std::make_unique<RenderFrameStore>();
    REQUIRE(store->begin(fixture.recorded, 18) == RenderPrepareStatus::Ready);
    std::array<RenderModelStatus, 2> status{};
    std::thread a([&] {
        auto frame = store->acquireEpoch(11, 18, 1);
        status[0] = frame.lease.prepareModel(fixture.current);
    });
    std::thread b([&] {
        auto frame = store->acquireEpoch(44, 18, 1);
        status[1] = frame.lease.prepareModel(second);
    });
    a.join(); b.join();
    CHECK(status[0] == RenderModelStatus::Ready);
    CHECK(status[1] == RenderModelStatus::Ready);
    auto frame = store->acquireEpoch(11, 18, 1);
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
    auto store = std::make_unique<RenderFrameStore>();
    REQUIRE(store->begin(fixture.recorded, 19) == RenderPrepareStatus::Ready);
    auto frame = store->acquireEpoch(11, 19, 1);
    REQUIRE(frame.lease);
    REQUIRE(frame.lease.prepareBones(fixture.current) == RenderModelStatus::Ready);
    CHECK_FALSE(store->acquire(11, 0, 1).lease);
    CHECK_FALSE(frame.lease.uploaded(0));
    const auto boundsBones = frame.lease.get()->animation.bones[0];
    fixture.current.visibility = 2u << 24;
    REQUIRE(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    CHECK(store->acquire(11, 2, 1).lease);
    CHECK(std::memcmp(&boundsBones, &frame.lease.get()->animation.bones[0], sizeof(boundsBones)) == 0);
    CHECK(frame.lease.prepareBones(fixture.current) == RenderModelStatus::Ready);
    fixture.current.renderOrigin[0] += 1;
    CHECK(frame.lease.prepareBones(fixture.current) == RenderModelStatus::OriginChanged);
}

TEST_CASE("historical bounds do not pin an early native origin before GPU preparation") {
    using namespace self_recall::model;
    FrameFixture fixture;
    auto store = std::make_unique<RenderFrameStore>();
    REQUIRE(store->begin(fixture.recorded, 20) == RenderPrepareStatus::Ready);
    auto frame = store->acquireEpoch(11, 20, 1);
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
    CHECK(store->acquire(11, 1, 1).lease);
    auto absolute = fixture.recorded.models[0];
    absolute.originRelative = 0;
    bounds.prepareHistorical(unit.data(), input, absolute);
    CHECK(bounds.unit[0x355] == std::byte{0xA4});
    CHECK(unit == untouched);
}

TEST_CASE("unused origins may change after upload while effective origin changes still fail") {
    FrameFixture fixture;
    auto store = std::make_unique<RenderFrameStore>();
    fixture.current.originRelative = fixture.recorded.models[0].originRelative = 0;
    REQUIRE(store->begin(fixture.recorded, 21) == RenderPrepareStatus::Ready);
    auto frame = store->acquireEpoch(11, 21, 1);
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
    auto store = std::make_unique<RenderFrameStore>();
    std::atomic<bool> done{false};
    std::atomic<unsigned> errors{0};
    std::atomic<unsigned> reads{0};
    std::array<std::thread, 2> workers;
    fixture.recorded.bones[0].words[12] = 1;
    REQUIRE(fixture.publish(*store, 1, 0) == RenderPrepareStatus::Ready);
    for (auto& worker : workers) worker = std::thread([&] {
        while (!done.load(std::memory_order_acquire)) {
            for (unsigned buffer = 0; buffer < 3; ++buffer) {
                auto frame = store->acquire(11, buffer, 1);
                if (!frame.lease) continue;
                const auto& animation = frame.lease.get()->animation;
                if (animation.header.key.serial != animation.bones[0].words[12]) ++errors;
                ++reads;
            }
        }
    });
    while (!reads.load(std::memory_order_relaxed)) std::this_thread::yield();
    for (std::uint64_t i = 2; i <= 1000; ++i) {
        fixture.recorded.bones[0].words[12] = static_cast<std::uint32_t>(i);
        while (fixture.publish(*store, i, static_cast<unsigned>((i - 1) % 3)) == RenderPrepareStatus::ReadersBusy)
            std::this_thread::yield();
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
    CHECK(input.prepare(view, changed, bones) == RenderInputStatus::Ready); // unused world-space origin
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
    auto store = std::make_unique<RenderFrameStore>();
    const totk::core::WorldPosition offset{-1.75f, 4.5f, 0.25f};
    REQUIRE(store->begin(fixture.recorded, 22, offset) == RenderPrepareStatus::Ready);
    auto frame = store->acquireEpoch(11, 22, 1);
    REQUIRE(frame.lease);
    float effect[12];
    REQUIRE(frame.lease.copyWorldBone(0, effect));
    RenderWristFrame wrist;
    REQUIRE(store->copyWrist(22, 1, wrist));
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
    auto store = std::make_unique<RenderFrameStore>();
    REQUIRE(store->begin(fixture.recorded, 23) == RenderPrepareStatus::Ready);
    auto frame = store->acquireEpoch(11, 23, 1);
    REQUIRE(frame.lease.prepareModel(fixture.current) == RenderModelStatus::Ready);
    CHECK(frame.lease.validateUploadedModel(fixture.current) == RenderModelStatus::Busy);
    frame.lease.markUploaded();
    const auto before = frame.lease.get()->animation.bones[0];
    fixture.current.renderOrigin[2] += 10; // candidate18's logged ten-metre origin-cell change
    CHECK(frame.lease.validateUploadedModel(fixture.current) == RenderModelStatus::Ready);
    CHECK(frame.lease.prepareModel(fixture.current) == RenderModelStatus::OriginChanged);
    CHECK(std::memcmp(&before, &frame.lease.get()->animation.bones[0], sizeof(before)) == 0);
    fixture.current.visibility = 1u << 24;
    CHECK(frame.lease.validateUploadedModel(fixture.current) == RenderModelStatus::BufferChanged);
    fixture.current.identity.resource += 1;
    CHECK(frame.lease.validateUploadedModel(fixture.current) == RenderModelStatus::ModelChanged);
}
