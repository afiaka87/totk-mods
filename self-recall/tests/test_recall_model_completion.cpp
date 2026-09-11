#include <array>
#include <memory>
#include <thread>
#include "RecallCompletedQueue.hpp"
#include "RecallModelCompletion.hpp"
#include "RecallPosePlayback.hpp"
#include "doctest.h"

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
    const auto report = fixture.inspect();
    REQUIRE(report.status == model::CompletedQueueStatus::Ready);
    CHECK(report.singles == 1); CHECK(report.groups == 1); CHECK(report.visited == 3);
    CHECK(fixture.views[0].pose.queueAdmission == 1);
    CHECK(fixture.views[1].pose.queueAdmission == 0);
    CHECK(fixture.views[2].pose.queueAdmission == 1);
    CHECK(fixture.queue == before);
    write(fixture.queue, 0x20, std::uint32_t{0});
    CHECK(fixture.inspect().status == model::CompletedQueueStatus::MissingBody);
    fixture.groups[0] = fixture.body.data();
    CHECK(fixture.inspect().status == model::CompletedQueueStatus::Ready);
}

TEST_CASE("empty and malformed native queues cannot admit stale animation") {
    QueueFixture fixture;
    write(fixture.queue, 0x58, std::uint32_t{0});
    CHECK(fixture.inspect().status == model::CompletedQueueStatus::Ready);
    CHECK(fixture.views[2].pose.queueAdmission == 0);
    write(fixture.queue, 0x20, std::uint32_t{0});
    CHECK(fixture.inspect().status == model::CompletedQueueStatus::MissingBody);
    write(fixture.queue, 0x20, std::uint32_t{1});
    fixture.singles[0] = nullptr;
    CHECK(fixture.inspect().status == model::CompletedQueueStatus::MissingQueue);
    fixture.singles[0] = fixture.root.data();
    write(fixture.cloth, 8, fixture.body.data());
    CHECK(fixture.inspect().status == model::CompletedQueueStatus::Limit);
    write(fixture.queue, 0x20, UINT32_MAX);
    CHECK(fixture.inspect().status == model::CompletedQueueStatus::Limit);
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
        REQUIRE(fixture.inspect().status == model::CompletedQueueStatus::Ready);
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
