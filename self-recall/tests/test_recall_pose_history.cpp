#include <atomic>
#include <memory>
#include <thread>

#include "RecallPoseHistory.hpp"
#include "doctest.h"

using namespace self_recall::pure;

namespace {
struct Fixture {
    std::unique_ptr<PoseHistorySlot[]> slots = std::make_unique<PoseHistorySlot[]>(4);
    std::unique_ptr<PosePayloadBlock[]> payload = std::make_unique<PosePayloadBlock[]>(4 * 36);
    PoseHistory history{slots.get(), 4, {payload.get(), 4 * 36}};
    RecordedModelPose models[2]{};
    RecordedBoneMatrix bones[3]{};
    PoseFrameInput input{};

    Fixture() {
        input.models = models;
        input.bones = bones;
        input.header.modelCount = 2;
        input.header.boneCount = 3;
        input.header.worldGeneration = 1;
        input.header.modelGeneration = 1;
        models[0].identity = {10, 20, 30, 0, 1, 0};
        models[1].identity = {11, 21, 31, 1, 2, 0};
        input.header.route.pose.rotation.values[0] = 1;
        input.header.route.pose.rotation.values[4] = 1;
        input.header.route.pose.rotation.values[8] = 1;
        at(1);
    }

    void at(std::uint32_t epoch, std::uint64_t nsPerFrame = 16666667) {
        input.header.frameEpoch = epoch;
        input.header.elapsedNanoseconds = epoch * nsPerFrame;
        input.header.route.pose.position.x = static_cast<float>(epoch);
        input.header.haveWrist = true;
        for (auto& v : input.header.wristMatrix) v = static_cast<float>(epoch);
        for (auto& model : models) {
            model.visibility = epoch;
            for (auto& v : model.renderOrigin) v = static_cast<float>(epoch * 2);
        }
        for (auto& bone : bones)
            for (auto& word : bone.words) word = epoch;
    }
};
}

TEST_CASE("pose history is bounded and rejects incomplete or oversized inputs") {
    CHECK(sizeof(PoseHistorySlot) * kHistoryCapacity + sizeof(PoseHistory) +
          kPosePayloadArenaBytes <= kPoseHistoryByteLimit);
    Fixture f;
    f.input.header.boneCount = kPoseBoneLimit + 1;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::InvalidInput);
    f.input.header.boneCount = 3;
    f.input.header.modelCount = 0;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::InvalidInput);
    f.input.header.modelCount = 2;
    f.input.models = nullptr;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::InvalidInput);
    CHECK(f.history.count() == 0);
    CHECK_FALSE(f.history.newest());
}

TEST_CASE("owned equipment changes retain history but a replaced body starts a new generation") {
    Fixture f;
    f.input.header.bodyModelCount = 1;
    const auto first = f.history.record(f.input);
    REQUIRE(first.status == PoseRecordStatus::Recorded);
    f.at(2);
    f.models[1].identity = {111, 121, 131, 1, 2, 0};
    auto second = f.history.record(f.input);
    CHECK(second.key.generation == first.key.generation);
    CHECK(f.history.count() == 2);
    auto old = f.history.acquire(first.key);
    REQUIRE(old);
    CHECK(old.get()->models[1].identity.unit == 11);
    old.release();
    f.at(3);
    f.input.header.modelCount = 1;
    f.input.header.boneCount = 1;
    CHECK(f.history.record(f.input).key.generation == first.key.generation);
    CHECK(f.history.count() == 3);
    f.at(4);
    f.input.header.modelCount = 2;
    f.input.header.boneCount = 3;
    CHECK(f.history.record(f.input).key.generation == first.key.generation);
    f.at(5);
    f.models[0].identity.skeleton = 333;
    CHECK(f.history.record(f.input).key.generation != first.key.generation);
    CHECK(f.history.count() == 1);
    CHECK_FALSE(f.history.acquire(first.key));
}

TEST_CASE("archive ownership cannot weaken the default roster rule or exceed the roster") {
    Fixture f;
    f.input.header.bodyModelCount = 3;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::InvalidInput);
    f.input.header.bodyModelCount = 0;
    auto first = f.history.record(f.input);
    REQUIRE(first.status == PoseRecordStatus::Recorded);
    f.at(2);
    f.models[1].identity.unit = 55;
    CHECK(f.history.record(f.input).key.generation != first.key.generation);
    f.at(3);
    f.input.header.bodyModelCount = 1;
    auto archived = f.history.record(f.input);
    CHECK(f.history.count() == 1);
    f.at(4);
    f.input.header.bodyModelCount = 0;
    CHECK(f.history.record(f.input).key.generation != archived.key.generation);
}

TEST_CASE("pose frames bind disjoint bone ranges to distinct native model identities") {
    Fixture f;
    f.models[1].identity.firstBone = 0;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::InvalidInput);
    f.models[1].identity.firstBone = 2;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::InvalidInput);
    f.models[1].identity.firstBone = 1;
    f.models[1].identity.unit = f.models[0].identity.unit;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::InvalidInput);
    f.models[1].identity.unit = 11;
    f.models[1].identity.boneCount = 1;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::InvalidInput);
    f.models[1].identity.boneCount = 2;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::Recorded);
    auto lease = f.history.newest();
    REQUIRE(lease);
    CHECK(lease.get()->models[1].identity.unit == 11);
    CHECK(lease.get()->models[1].identity.firstBone == 1);
    CHECK(lease.get()->models[1].identity.boneCount == 2);
    const auto oldKey = lease.get()->header.key;
    f.at(2);
    f.models[1].identity.resource = 555;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::ReaderBusy);
    CHECK(lease.get()->models[1].identity.resource == 31);
    lease.release();
    const auto replacement = f.history.record(f.input);
    REQUIRE(replacement.status == PoseRecordStatus::Recorded);
    CHECK(replacement.key.generation != oldKey.generation);
    CHECK_FALSE(f.history.acquire(oldKey));
    CHECK(f.history.count() == 1);
}

TEST_CASE("pose history retains stationary gestures and real elapsed time") {
    Fixture f;
    for (std::uint32_t epoch = 1; epoch <= 540; ++epoch) {
        f.at(epoch, 33333333);
        f.input.header.route.pose.position.x = 10;
        REQUIRE(f.history.record(f.input).status == PoseRecordStatus::Recorded);
    }
    auto newest = f.history.newest();
    auto earlier = f.history.newest(3);
    REQUIRE(newest);
    REQUIRE(earlier);
    CHECK(newest.get()->header.route.pose.position.x == earlier.get()->header.route.pose.position.x);
    CHECK(newest.get()->bones[2].words[15] == 540);
    CHECK(earlier.get()->bones[2].words[15] == 537);
    CHECK(newest.get()->header.elapsedNanoseconds - earlier.get()->header.elapsedNanoseconds == 99999999);
    CHECK(f.history.count() == 4);
}

TEST_CASE("duplicate and reversed frame clocks never commit another pose") {
    Fixture f;
    const auto first = f.history.record(f.input);
    REQUIRE(first.status == PoseRecordStatus::Recorded);
    f.input.header.elapsedNanoseconds += 1;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::DuplicateFrame);
    f.at(2);
    f.input.header.elapsedNanoseconds = 1;
    CHECK(f.history.record(f.input).status == PoseRecordStatus::TimeWentBackwards);
    CHECK(f.history.count() == 1);
    CHECK(f.history.newest().get()->header.key == first.key);
}

TEST_CASE("readers pin complete frames through ring wrap and release permits progress") {
    Fixture f;
    const auto first = f.history.record(f.input);
    auto reader = f.history.acquire(first.key);
    auto secondReader = f.history.acquire(first.key);
    REQUIRE(reader);
    REQUIRE(secondReader);
    for (std::uint32_t epoch = 2; epoch <= 4; ++epoch) {
        f.at(epoch);
        REQUIRE(f.history.record(f.input).status == PoseRecordStatus::Recorded);
    }
    f.at(5);
    CHECK(f.history.record(f.input).status == PoseRecordStatus::ReaderBusy);
    reader.release();
    CHECK(f.history.record(f.input).status == PoseRecordStatus::ReaderBusy);
    CHECK(secondReader.get()->bones[0].words[0] == 1);
    secondReader.release();
    REQUIRE(f.history.record(f.input).status == PoseRecordStatus::Recorded);
    CHECK_FALSE(f.history.acquire(first.key));
    CHECK(f.history.newest(3).get()->header.frameEpoch == 2);
}

TEST_CASE("scene clear invalidates keys without overwriting leased bytes") {
    Fixture f;
    const auto first = f.history.record(f.input);
    auto old = f.history.acquire(first.key);
    REQUIRE(old);
    f.history.clear();
    CHECK_FALSE(f.history.newest());
    CHECK_FALSE(f.history.acquire(first.key));
    CHECK(old.get()->bones[0].words[0] == 1);
    f.at(20);
    CHECK(f.history.record(f.input).status == PoseRecordStatus::ReaderBusy);
    old.release();
    const auto next = f.history.record(f.input);
    CHECK(next.status == PoseRecordStatus::Recorded);
    CHECK(next.key.generation != first.key.generation);
    CHECK(next.key.serial == 1);
}

TEST_CASE("a replaced model set starts a new coherent history generation") {
    Fixture f;
    const auto first = f.history.record(f.input);
    f.at(2);
    f.input.header.modelGeneration = 2;
    const auto next = f.history.record(f.input);
    REQUIRE(next.status == PoseRecordStatus::Recorded);
    CHECK(f.history.count() == 1);
    CHECK_FALSE(f.history.acquire(first.key));
    CHECK_FALSE(f.history.newest(1));
    CHECK(f.history.newest().get()->header.modelGeneration == 2);
}

TEST_CASE("concurrent pose readers never observe a mixed frame") {
    Fixture f;
    REQUIRE(f.history.record(f.input).status == PoseRecordStatus::Recorded);
    std::atomic<bool> stop{false};
    std::atomic<std::uint32_t> ready{0}, errors{0}, reads{0};
    auto consume = [&] {
        ready.fetch_add(1);
        while (!stop.load()) {
            auto lease = f.history.newest();
            if (!lease) continue;
            const auto& frame = *lease.get();
            const auto epoch = static_cast<std::uint32_t>(frame.header.frameEpoch);
            bool valid = frame.header.route.pose.position.x == static_cast<float>(epoch);
            valid &= frame.header.elapsedNanoseconds == epoch * std::uint64_t{16666667};
            for (const auto value : frame.header.wristMatrix)
                valid &= value == static_cast<float>(epoch);
            for (std::uint16_t i = 0; i < frame.header.modelCount; ++i) {
                valid &= frame.models[i].visibility == epoch;
                for (const auto value : frame.models[i].renderOrigin)
                    valid &= value == static_cast<float>(epoch * 2);
            }
            for (std::uint16_t i = 0; i < frame.header.boneCount; ++i)
                for (const auto word : frame.bones[i].words) valid &= word == epoch;
            if (!valid) errors.fetch_add(1);
            reads.fetch_add(1);
        }
    };
    std::thread body(consume), wrist(consume);
    while (ready.load() != 2 || reads.load() < 2) std::this_thread::yield();
    for (std::uint32_t epoch = 2; epoch <= 10000; ++epoch) {
        f.at(epoch);
        f.input.header.worldGeneration = 1 + epoch / 1000;
        while (f.history.record(f.input).status == PoseRecordStatus::ReaderBusy)
            std::this_thread::yield();
    }
    stop.store(true);
    body.join();
    wrist.join();
    CHECK(errors.load() == 0);
    CHECK(reads.load() >= 2);
    REQUIRE(f.history.newest());
    CHECK(f.history.newest().get()->header.frameEpoch == 10000);
}
