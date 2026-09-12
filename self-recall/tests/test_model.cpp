#include <array>
#include <memory>
#include "RecallModelEngine.hpp"
#include "doctest.h"
#include <thread>
#include "RecallRender.hpp"
#include "RecallPlayback.hpp"
#include <cstring>

namespace self_recall_tests::test_recall_model_capture {
namespace pure = self_recall::pure;
namespace model = self_recall::model;

TEST_CASE("completed model capture requires the whole roster and owns the copied bytes") {
    constexpr std::array<std::uint16_t, 5> counts{75, 47, 16, 31, 1};
    auto source = std::make_unique<pure::RecordedBoneMatrix[]>(170);
    auto workspace = std::make_unique<model::CaptureWorkspace>();
    auto storage = std::make_unique<pure::PoseHistorySlot[]>(2);
    std::unique_ptr<pure::PosePayloadBlock[]> payload = std::make_unique<pure::PosePayloadBlock[]>(2 * 36);
    pure::PoseHistory history{storage.get(), 2, {payload.get(), 2 * 36}};
    std::array<model::View, 5> views{};
    std::array<std::array<std::uint32_t, 3>, 5> boneVisible{};
    std::array<std::array<std::uint32_t, 2>, 5> materialVisible{};
    constexpr std::array<std::uint16_t, 5> materials{35, 9, 0, 2, 1};
    std::uint16_t offset = 0;
    for (std::size_t i = 0; i < views.size(); ++i) {
        views[i].identity = {100 + i, 200 + i, 300 + i, counts[i], materials[i]};
        views[i].boneBytes = source.get() + offset;
        views[i].boneVisibility = boneVisible[i].data();
        views[i].materialVisibility = materialVisible[i].data();
        boneVisible[i][0] = 1;
        materialVisible[i][0] = 1;
        views[i].pose.visibility = static_cast<std::uint32_t>(i);
        offset = static_cast<std::uint16_t>(offset + counts[i]);
    }
    views[0].wristIndex = 3;
    source[3].words[12] = 0x3f800000u;
    pure::PoseFrameHeader header{};
    header.worldGeneration = 1;
    header.modelGeneration = 1;
    header.frameEpoch = 42;
    header.elapsedNanoseconds = 700000000;
    header.modelCount = 5;
    header.boneCount = 170;
    header.materialCount = 47;
    boneVisible[0][2] = 1u << 10;
    materialVisible[0][1] = 1u << 2;

    CHECK(model::recordCompleted(header, std::span(views).first(4), *workspace, history).status ==
          model::CaptureStatus::ModelCountMismatch);
    CHECK(history.count() == 0);
    views[4].boneBytes = nullptr;
    CHECK(model::recordCompleted(header, views, *workspace, history).status ==
          model::CaptureStatus::BoneRangeMismatch);
    views[4].boneBytes = source.get() + 169;
    views[4].boneVisibility = nullptr;
    CHECK(model::recordCompleted(header, views, *workspace, history).status ==
          model::CaptureStatus::MissingVisibility);
    views[4].boneVisibility = boneVisible[4].data();
    views[0].wristIndex = -1;
    CHECK(model::recordCompleted(header, views, *workspace, history).status ==
          model::CaptureStatus::MissingWrist);
    views[0].wristIndex = 3;
    const auto report = model::recordCompleted(header, views, *workspace, history);
    REQUIRE(report.status == model::CaptureStatus::Recorded);
    auto frame = history.acquire(report.history.key);
    REQUIRE(frame);
    CHECK(frame.get()->header.wristMatrix[3] == 1);
    CHECK(frame.get()->models[3].identity.firstBone == 138);
    CHECK(frame.get()->models[3].identity.boneCount == 31);
    CHECK(frame.get()->models[3].identity.unit == 103);
    CHECK(frame.get()->models[4].identity.firstBone == 169);
    CHECK(frame.get()->models[1].identity.firstMaterial == 35);
    CHECK(frame.get()->models[3].identity.firstMaterial == 44);
    CHECK(pure::visibilityBit(frame.get()->visible.bones, 74));
    CHECK(pure::visibilityBit(frame.get()->visible.bones, 75));
    CHECK_FALSE(pure::visibilityBit(frame.get()->visible.bones, 76));
    CHECK(pure::visibilityBit(frame.get()->visible.bones, 169));
    CHECK(pure::visibilityBit(frame.get()->visible.materials, 34));
    CHECK(pure::visibilityBit(frame.get()->visible.materials, 35));
    CHECK(pure::visibilityBit(frame.get()->visible.materials, 44));
    CHECK_FALSE(pure::visibilityBit(frame.get()->visible.materials, 45));
    boneVisible[0][2] = 0;
    materialVisible[0][1] = 0;
    CHECK(pure::visibilityBit(frame.get()->visible.bones, 74));
    CHECK(pure::visibilityBit(frame.get()->visible.materials, 34));
    source[3].words[12] = 0;
    views[3].identity.unit = 999;
    CHECK(frame.get()->bones[3].words[12] == 0x3f800000u);
    CHECK(frame.get()->models[3].identity.unit == 103);
    views[3].identity.unit = 103;
    CHECK(model::recordCompleted(header, views, *workspace, history).history.status ==
          pure::PoseRecordStatus::DuplicateFrame);
    frame.release();
    ++header.frameEpoch;
    header.elapsedNanoseconds += 16666667;
    ++views[4].identity.materialCount;
    ++header.materialCount;
    const auto replaced = model::recordCompleted(header, views, *workspace, history);
    REQUIRE(replaced.status == model::CaptureStatus::Recorded);
    CHECK(replaced.history.key.generation != report.history.key.generation);
    CHECK_FALSE(history.acquire(report.history.key));
    CHECK(history.count() == 1);
    ++header.materialCount;
    CHECK(model::recordCompleted(header, views, *workspace, history).status ==
          model::CaptureStatus::MaterialRangeMismatch);
}
}

namespace self_recall_tests::test_recall_model_completion {
using namespace self_recall;
using pure::ModelQueueLane;
using pure::ModelJoinStatus;

namespace {
template<class T, std::size_t N>
void write(std::array<std::byte, N>& bytes, unsigned offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
struct QueueFixture {
    std::array<std::byte, 0xC0> queue{};
    std::array<std::byte, 0x30> root{};
    std::array<std::byte, 0x20> body{}, cloth{}, equipment{};
    std::array<const void*, 1> heads{body.data()}, singles{root.data()}, groups{equipment.data()};
    std::array<model::View, 3> views{};
    QueueFixture() {
        write(queue, 0x20, std::uint32_t{1}); write(queue, 0x28, singles.data());
        write(queue, 0x58, std::uint32_t{1}); write(queue, 0x60, groups.data());
        write(root, 0x20, 1); write(root, 0x28, heads.data());
        write(body, 0, std::uintptr_t{11}); write(body, 8, cloth.data());
        write(body, 0x1E, std::uint8_t{0x40});
        write(cloth, 0, std::uintptr_t{22});
        write(equipment, 0, std::uintptr_t{33}); write(equipment, 0x1E, std::uint8_t{0x40});
        for (unsigned i = 0; i < views.size(); ++i) views[i].identity = {11 * (i + 1), 100 + i, 200 + i, 1, 0};
    }
    auto inspect() { return model::inspectCompletedQueue(queue.data(), views, 2); }
};
}

TEST_CASE("model completion joins both lanes once in either order and isolates scenes and epochs") {
    for (const auto first : {ModelQueueLane::Single, ModelQueueLane::Multi}) {
        const auto second = first == ModelQueueLane::Single ? ModelQueueLane::Multi : ModelQueueLane::Single;
        pure::ModelCompletionJoin<2> join;
        join.beginFrame(7);
        REQUIRE(join.registerScene(100, 7)); REQUIRE(join.registerScene(200, 7));
        CHECK_FALSE(join.registerScene(300, 7));
        CHECK(join.complete(100, 7, first) == ModelJoinStatus::Waiting);
        CHECK(join.complete(100, 7, first) == ModelJoinStatus::Waiting);
        CHECK(join.complete(200, 7, second) == ModelJoinStatus::Waiting);
        CHECK(join.complete(100, 7, second) == ModelJoinStatus::Complete);
        CHECK(join.complete(100, 7, second) == ModelJoinStatus::Waiting);
        CHECK(join.complete(200, 7, first) == ModelJoinStatus::Complete);
        CHECK(join.complete(300, 7, first) == ModelJoinStatus::UnknownScene);
        join.beginFrame(8);
        CHECK(join.complete(100, 7, first) == ModelJoinStatus::WrongEpoch);
        CHECK(join.complete(100, 8, second) == ModelJoinStatus::UnknownScene);
        REQUIRE(join.registerScene(100, 8));
        CHECK(join.complete(100, 8, second) == ModelJoinStatus::Waiting);
        CHECK(join.complete(100, 8, first) == ModelJoinStatus::Complete);
    }
}

TEST_CASE("concurrent final workers publish both lanes' writes before a single observer") {
    pure::ModelCompletionJoin<> join;
    for (unsigned epoch = 1; epoch <= 128; ++epoch) {
        join.beginFrame(epoch); REQUIRE(join.registerScene(100, epoch));
        std::array<unsigned, 2> payload{};
        std::atomic<unsigned> published{0}, observed{0};
        const auto worker = [&](unsigned i, ModelQueueLane lane) {
            payload[i] = i + 1;
            if (join.complete(100, epoch, lane) == ModelJoinStatus::Complete) {
                observed.store(payload[0] + payload[1]);
                published.fetch_add(1);
            }
        };
        std::thread first(worker, 0, ModelQueueLane::Single), second(worker, 1, ModelQueueLane::Multi);
        first.join(); second.join();
        CHECK(published.load() == 1); CHECK(observed.load() == 3);
    }
}

TEST_CASE("completed queue membership covers single body, linked clothing and multi equipment") {
    QueueFixture fixture;
    const auto before = fixture.queue;
    REQUIRE(fixture.inspect() == model::CompletedQueueStatus::Ready);
    CHECK(fixture.views[0].pose.queueAdmission == 1);
    CHECK(fixture.views[1].pose.queueAdmission == 0);
    CHECK(fixture.views[2].pose.queueAdmission == 1);
    CHECK(fixture.queue == before);
    write(fixture.queue, 0x20, std::uint32_t{0});
    CHECK(fixture.inspect() == model::CompletedQueueStatus::MissingBody);
    fixture.groups[0] = fixture.body.data();
    CHECK(fixture.inspect() == model::CompletedQueueStatus::Ready);
}

TEST_CASE("empty and malformed native queues cannot admit stale animation") {
    QueueFixture fixture;
    write(fixture.queue, 0x58, std::uint32_t{0});
    CHECK(fixture.inspect() == model::CompletedQueueStatus::Ready);
    CHECK(fixture.views[2].pose.queueAdmission == 0);
    write(fixture.queue, 0x20, std::uint32_t{0});
    CHECK(fixture.inspect() == model::CompletedQueueStatus::MissingBody);
    write(fixture.queue, 0x20, std::uint32_t{1});
    fixture.singles[0] = nullptr;
    CHECK(fixture.inspect() == model::CompletedQueueStatus::MissingQueue);
    fixture.singles[0] = fixture.root.data();
    write(fixture.cloth, 8, fixture.body.data());
    CHECK(fixture.inspect() == model::CompletedQueueStatus::Limit);
    write(fixture.queue, 0x20, UINT32_MAX);
    CHECK(fixture.inspect() == model::CompletedQueueStatus::Limit);
}

TEST_CASE("both native lanes feed complete animated history that can start and step backward") {
    QueueFixture fixture;
    pure::ModelCompletionJoin<> join;
    auto slots = std::make_unique<pure::PoseHistorySlot[]>(61);
    std::unique_ptr<pure::PosePayloadBlock[]> payload = std::make_unique<pure::PosePayloadBlock[]>(61 * 36);
    pure::PoseHistory history(slots.get(), 61, {payload.get(), 61 * 36});
    auto workspace = std::make_unique<model::CaptureWorkspace>();
    std::array<pure::RecordedBoneMatrix, 3> bones{};
    std::uint32_t visible = 1;
    for (unsigned i = 0; i < fixture.views.size(); ++i) {
        fixture.views[i].boneBytes = &bones[i];
        fixture.views[i].boneVisibility = &visible;
    }
    fixture.views[0].wristIndex = 0;
    pure::GameTime time;
    pure::GameTimeSnapshot clock;
    for (unsigned epoch = 1; epoch <= 61; ++epoch) {
        clock = time.update(1.0f, false);
        join.beginFrame(epoch); REQUIRE(join.registerScene(100, epoch));
        CHECK(join.complete(100, epoch, ModelQueueLane::Multi) == ModelJoinStatus::Waiting);
        REQUIRE(join.complete(100, epoch, ModelQueueLane::Single) == ModelJoinStatus::Complete);
        REQUIRE(fixture.inspect() == model::CompletedQueueStatus::Ready);
        pure::PoseFrameHeader header{};
        header.worldGeneration = header.modelGeneration = 1;
        header.frameEpoch = epoch; header.elapsedNanoseconds = clock.elapsedNanoseconds;
        header.modelCount = header.boneCount = 3; header.route.flags = pure::SampleAdmissible;
        header.route.pose.position.x = float(epoch);
        for (unsigned i = 0; i < bones.size(); ++i) {
            const float translation = float(epoch * 10 + i);
            std::memcpy(&bones[i].words[12], &translation, sizeof(translation));
        }
        REQUIRE(model::recordCompleted(header, fixture.views, *workspace, history).status == model::CaptureStatus::Recorded);
    }
    pure::PosePlayback playback;
    REQUIRE(playback.begin(history, 1, clock) == pure::PosePlaybackStatus::Ready);
    CHECK(playback.selectedFrame()->header.route.pose.position.x == 61);
    clock.elapsedNanoseconds += 100000000;
    REQUIRE(playback.step(clock, 1) == pure::PosePlaybackStatus::Ready);
    CHECK(playback.selectedFrame()->header.route.pose.position.x == 58);
    CHECK(playback.selectedFrame()->header.wristMatrix[3] == 580);
    CHECK(playback.selectedFrame()->models[2].queueAdmission == 1);
    float equipmentTranslation;
    std::memcpy(&equipmentTranslation, &playback.selectedFrame()->bones[2].words[12], sizeof(float));
    CHECK(equipmentTranslation == 582);
}
}

namespace self_recall_tests::test_recall_native_admission {
using namespace self_recall;

namespace {
template<class T, std::size_t N>
void write(std::array<std::byte, N>& target, unsigned offset, T value) {
    std::memcpy(target.data() + offset, &value, sizeof(value));
}

struct AdmissionFixture {
    std::array<std::byte, 0x42D8> scene{};
    std::array<std::byte, 0x244> body{}, glider{};
    std::array<std::uintptr_t, 3> units{11, 22, 33};
    std::array<const void*, 1> bodyEntries{&units[0]};
    std::array<const void*, 2> gliderEntries{&units[1], &units[2]};
    std::array<const void*, 2> roots{body.data(), glider.data()};
    std::array<pure::RecordedModelPose, 3> models{};
    AdmissionFixture() {
        write(scene, 0x42A8, std::uint8_t{1});
        write(scene, 0x42C4, 4);
        write(scene, 0x42D4, 4);
        write(body, 0x60, scene.data());
        write(glider, 0x60, scene.data());
        write(body, 0x20, 1);
        write(glider, 0x20, 2);
        write(body, 0x28, bodyEntries.data());
        write(glider, 0x28, gliderEntries.data());
        for (unsigned i = 0; i < models.size(); ++i) {
            models[i].identity.unit = units[i];
            models[i].queueAdmission = 1;
        }
    }
    auto plan() { return model::planNativeAdmission(scene.data(), roots, models); }
};
}

TEST_CASE("historically drawn retained models enter their native queue lanes once") {
    AdmissionFixture fixture;
    const auto beforeBody = fixture.body;
    const auto beforeGlider = fixture.glider;
    const auto beforeScene = fixture.scene;
    auto plan = fixture.plan();
    REQUIRE(plan.status == model::AdmissionStatus::Ready);
    CHECK(plan.count == 2);
    CHECK(plan.request[0] == fixture.body.data());
    CHECK(plan.request[1] == fixture.glider.data());
    CHECK(fixture.body == beforeBody);
    CHECK(fixture.glider == beforeGlider);
    CHECK(fixture.scene == beforeScene);
    write(fixture.body, 0x241, std::uint8_t{2});
    plan = fixture.plan();
    REQUIRE(plan.status == model::AdmissionStatus::Ready);
    CHECK(plan.count == 1);
    CHECK(plan.request[0] == fixture.glider.data());
    fixture.models[1].queueAdmission = fixture.models[2].queueAdmission = 0;
    CHECK(fixture.plan().count == 0);
}

TEST_CASE("native admission rejects destroyed roots and exhausted or closed queues without mutation") {
    AdmissionFixture fixture;
    write(fixture.glider, 0x241, std::uint8_t{1});
    CHECK(fixture.plan().status == model::AdmissionStatus::DetachedModel);
    write(fixture.glider, 0x241, std::uint8_t{0});
    write(fixture.glider, 0x240, std::uint8_t{8});
    CHECK(fixture.plan().status == model::AdmissionStatus::DetachedModel);
    write(fixture.glider, 0x240, std::uint8_t{0});
    write(fixture.scene, 0x42D0, 4);
    CHECK(fixture.plan().status == model::AdmissionStatus::QueueFull);
    write(fixture.glider, 0x241, std::uint8_t{2});
    CHECK(fixture.plan().status == model::AdmissionStatus::Ready);
    write(fixture.scene, 0x42A8, std::uint8_t{0});
    CHECK(fixture.plan().status == model::AdmissionStatus::ClosedQueue);
}

TEST_CASE("native admission requires current scene ownership and every recorded unit") {
    AdmissionFixture fixture;
    write(fixture.glider, 0x60, static_cast<const void*>(nullptr));
    CHECK(fixture.plan().status == model::AdmissionStatus::WrongScene);
    write(fixture.glider, 0x60, fixture.scene.data());
    fixture.models[2].identity.unit = 99;
    CHECK(fixture.plan().status == model::AdmissionStatus::MissingUnit);
    fixture.models[2].identity.unit = 33;
    fixture.roots[1] = fixture.roots[0];
    CHECK(fixture.plan().status == model::AdmissionStatus::InvalidRoster);
}
}

namespace self_recall_tests::test_recall_equipment_links {
using namespace self_recall::model;

TEST_CASE("fused attachment ownership follows collected equipment but rejects foreign parents") {
    int player, sword, shield, droppedWeapon;
    std::array<const void*, 2> owned{&sword, &shield};
    CHECK(isEquipmentParent(&player, &player, {}));
    CHECK(isEquipmentParent(&sword, &player, owned));
    CHECK(isEquipmentParent(&shield, &player, owned));
    CHECK_FALSE(isEquipmentParent(&droppedWeapon, &player, owned));
    CHECK_FALSE(isEquipmentParent(nullptr, &player, owned));
    CHECK_FALSE(isEquipmentParent(&sword, &player, std::span(owned).subspan(1)));
}

TEST_CASE("equipment traversal includes all static clothing and sheathed links") {
    std::array<std::byte, 0x3EA8> equipment{};
    std::array<unsigned, 4> counts{};
    unsigned visited = 0;
    for (unsigned i = 0; i < 20; ++i) {
        const unsigned id = 100 + i;
        std::memcpy(equipment.data() + 0x20 + 0x18 * i + 0x10, &id, sizeof(id));
    }
    CHECK(visitEquipmentLinks(equipment.data(), [&](const void* link, EquipmentLinkKind kind, unsigned slot) {
        ++visited;
        ++counts[static_cast<unsigned>(kind)];
        if (kind == EquipmentLinkKind::Dynamic || kind == EquipmentLinkKind::Static) {
            unsigned id;
            std::memcpy(&id, static_cast<const std::byte*>(link) + 0x10, sizeof(id));
            CHECK(id == 100 + slot + (kind == EquipmentLinkKind::Static ? 8 : 0));
        }
        return true;
    }));
    CHECK(counts == std::array<unsigned, 4>{8, 12, 8, 1});
    CHECK(visited + 3 + 8 == kOwnedActorLimit);
    visited = 0;
    CHECK_FALSE(visitEquipmentLinks(equipment.data(), [&](const void*, EquipmentLinkKind, unsigned) {
        return ++visited < 11;
    }));
    CHECK(visited == 11);
}

TEST_CASE("clothing can bind to Player through either native bone binding component") {
    std::array<std::byte, 0x428> registry{};
    std::array<std::byte, 0x88> bind{}, sameBone{};
    const auto readPointer = [](const void* p, std::size_t offset) {
        const void* value;
        std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
        return value;
    };
    const auto setPointer = [&](unsigned offset, const void* value) {
        std::memcpy(registry.data() + offset, &value, sizeof(value));
    };
    const void* expected = bind.data() + 0x70;
    const auto matches = [&](const void* link) { return link == expected; };
    CHECK_FALSE(hasBoundEquipmentParent(registry.data(), readPointer, matches));
    setPointer(0x18, bind.data());
    CHECK(hasBoundEquipmentParent(registry.data(), readPointer, matches));
    expected = sameBone.data() + 0x70;
    CHECK_FALSE(hasBoundEquipmentParent(registry.data(), readPointer, matches));
    setPointer(0x420, sameBone.data());
    CHECK(hasBoundEquipmentParent(registry.data(), readPointer, matches));
    CHECK_FALSE(hasBoundEquipmentParent(nullptr, readPointer, matches));
}

TEST_CASE("active fused equipment uses the weapon link rather than the player creation cache") {
    std::array<std::byte, 0x210> registry{};
    std::array<std::byte, 0x510> weapon{};
    std::array<std::byte, 0x3EA8> playerEquipment{};
    const auto readPointer = [](const void* p, std::size_t offset) {
        const void* value;
        std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
        return value;
    };
    const auto readByte = [](const void* p, std::size_t offset) {
        return static_cast<const std::uint8_t*>(p)[offset];
    };
    CHECK(fusedEquipmentLink(nullptr, readPointer, readByte) == nullptr);
    CHECK(fusedEquipmentLink(registry.data(), readPointer, readByte) == nullptr);
    const void* component = weapon.data();
    std::memcpy(registry.data() + 0x208, &component, sizeof(component));
    CHECK(fusedEquipmentLink(registry.data(), readPointer, readByte) == nullptr);
    weapon[0x50C] = std::byte{1};
    const auto* link = fusedEquipmentLink(registry.data(), readPointer, readByte);
    CHECK(link == weapon.data() + 0xB0);
    CHECK(link != playerEquipment.data() + 0x580);
    CHECK(link != playerEquipment.data() + 0x3E90);
    weapon[0x50C] = std::byte{0};
    CHECK(fusedEquipmentLink(registry.data(), readPointer, readByte) == nullptr);
}
}
