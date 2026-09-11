#include <memory>
#include <vector>
#include <chrono>
#include <iostream>
#include "RecallPoseHistory.hpp"
#include "RecallGameTime.hpp"
#include "RecallPosePlayback.hpp"
#include "doctest.h"

using namespace self_recall::pure;

namespace {
struct Input {
    std::array<RecordedModelPose, kPoseModelLimit> models{};
    std::array<RecordedBoneMatrix, kPoseBoneLimit> bones{};
    PoseFrameInput value{};
    Input(unsigned modelCount = 1, unsigned boneCount = 1) {
        value.models = models.data(); value.bones = bones.data();
        auto& h = value.header;
        h.modelCount = static_cast<std::uint16_t>(modelCount);
        h.boneCount = static_cast<std::uint16_t>(boneCount);
        h.materialCount = kPoseMaterialLimit;
        h.worldGeneration = h.modelGeneration = 1;
        h.route.flags = SampleAdmissible;
        for (unsigned i = 0; i < modelCount; ++i) {
            auto& m = models[i];
            m.identity = {i + 1u, i + 101u, i + 201u,
                static_cast<std::uint16_t>(i), static_cast<std::uint16_t>(i + 1 == modelCount ? boneCount - i : 1),
                static_cast<std::uint16_t>(i), static_cast<std::uint16_t>(i + 1 == modelCount ? kPoseMaterialLimit - i : 1)};
            m.visibility = i; m.queueAdmission = i % 2;
        }
        for (unsigned i = 0; i < 16; ++i) {
            value.visible.bones[i] = 0xf0f00000u + i;
            value.visible.materials[i] = 0xcccc0000u + i;
        }
        at(1);
    }
    void at(unsigned epoch, unsigned fps = 60) {
        value.header.frameEpoch = epoch;
        value.header.elapsedNanoseconds = std::uint64_t(epoch) * 1'000'000'000 / fps;
        value.header.route.pose.position.x = float(epoch);
    }
};
}

TEST_CASE("route headers remain available with every pose decoder pinned and reject stale keys") {
    std::array<PoseHistorySlot, 8> slots;
    std::array<PosePayloadBlock, 8> blocks;
    auto history = std::make_unique<PoseHistory>(slots.data(), 8, std::span{blocks});
    Input input;
    PoseFrameKey anchor;
    for (unsigned epoch = 1; epoch <= 8; ++epoch) {
        input.at(epoch);
        auto report = history->record(input.value);
        REQUIRE(report.status == PoseRecordStatus::Recorded);
        anchor = report.key;
    }
    std::array<PoseReadLease, kPoseReadBufferCount> pinned;
    for (auto& lease : pinned) { lease = history->acquire(anchor); REQUIRE(lease); }
    const auto failures = history->readFailures();
    for (unsigned back = 0; back < 8; ++back) {
        PoseFrameHeader header;
        REQUIRE(history->copyHeaderBefore(anchor, back, header));
        CHECK(header.frameEpoch == 8 - back);
        CHECK(header.route.pose.position.x == float(8 - back));
        CHECK(history->contains(header.key));
    }
    CHECK(history->readFailures() == failures);
    PoseFrameHeader header;
    CHECK_FALSE(history->copyHeaderBefore(anchor, 8, header));
    for (auto& lease : pinned) lease.release();
    input.at(9);
    REQUIRE(history->record(input.value).status == PoseRecordStatus::Recorded);
    CHECK_FALSE(history->copyHeader({1, anchor.generation, 0}, header));
    CHECK(history->copyHeader(anchor, header));
    history->clear();
    CHECK_FALSE(history->contains(anchor));
    CHECK_FALSE(history->copyHeaderBefore(anchor, 0, header));
}

TEST_CASE("pose codec preserves arbitrary IEEE words visibility and model fields exactly") {
    Input input(32, 512);
    std::array<std::byte, kPosePayloadMaxBytes> bytes{};
    auto decoded = std::make_unique<RecordedPoseFrame>();
    const std::uint32_t edge[] = {0, 0x3f800000, 0x80000000, 0x7fc12345, 0xff812345,
        0x7f800000, 0xff800000, 1, 0x007fffff, 0x3f800001};
    std::uint32_t random = 1234567;
    for (unsigned mode = 0; mode < 3; ++mode) {
        for (auto& bone : input.bones) for (unsigned j = 0; j < 16; ++j) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            bone.words[j] = mode == 0 ? random : mode == 1 ? edge[j % 10] : j % 2 ? 0x3f800000 : 0;
        }
        const auto size = encodePosePayload(input.value, bytes);
        REQUIRE(size);
        CHECK(size <= 4 + sizeof(RecordedVisibility) + 32 * sizeof(RecordedModelPose) + 512 * sizeof(RecordedBoneMatrix));
        decoded->header = input.value.header;
        REQUIRE(decodePosePayload({bytes.data(), size}, *decoded));
        CHECK(std::memcmp(decoded->bones, input.bones.data(), sizeof(decoded->bones)) == 0);
        CHECK(std::memcmp(decoded->models, input.models.data(), sizeof(decoded->models)) == 0);
        CHECK(std::memcmp(&decoded->visible, &input.value.visible, sizeof(decoded->visible)) == 0);
        CHECK_FALSE(decodePosePayload({bytes.data(), size - 1}, *decoded));
        CHECK(encodePosePayload(input.value, {bytes.data(), size - 1}) == 0);
    }
    bytes[0] = std::byte{7};
    CHECK_FALSE(decodePosePayload(bytes, *decoded));
}

TEST_CASE("full pose payload pool preserves old frames and recovers after ordinary expiry") {
    auto slots = std::make_unique<PoseHistorySlot[]>(4);
    auto blocks = std::make_unique<PosePayloadBlock[]>(2);
    auto history = std::make_unique<PoseHistory>(slots.get(), 4, std::span{blocks.get(), 2u});
    Input input;
    const auto first = history->record(input.value);
    REQUIRE(first.status == PoseRecordStatus::Recorded);
    auto reader = history->acquire(first.key);
    input.at(2); REQUIRE(history->record(input.value).status == PoseRecordStatus::Recorded);
    input.at(3); CHECK(history->record(input.value).status == PoseRecordStatus::StorageFull);
    CHECK(history->count() == 2);
    CHECK(history->newest().get()->header.frameEpoch == 2);
    CHECK(reader.get()->visible.bones[0] == 0xf0f00000u);
    history->trimToWindow(0);
    CHECK(history->count() == 1);
    CHECK(history->payloadUsage().liveAllocated == 2048);
    CHECK_FALSE(history->acquire(first.key));
    reader.release();
    REQUIRE(history->record(input.value).status == PoseRecordStatus::Recorded);
    CHECK(history->newest().get()->header.frameEpoch == 3);
    CHECK(history->storageFailures() == 1);
}

TEST_CASE("pool exhaustion cannot freeze expiry when the recording clock advances") {
    std::array<PoseHistorySlot, 4> slots;
    std::array<PosePayloadBlock, 1> blocks;
    auto history = std::make_unique<PoseHistory>(slots.data(), 4, std::span{blocks});
    Input input;
    REQUIRE(history->record(input.value).status == PoseRecordStatus::Recorded);
    input.at(2);
    CHECK(history->record(input.value).status == PoseRecordStatus::StorageFull);
    input.at(65 * 60);
    REQUIRE(history->record(input.value).status == PoseRecordStatus::Recorded);
    CHECK(history->count() == 1);
    CHECK(history->newest().get()->header.frameEpoch == 65 * 60);
}

TEST_CASE("pose block reuse handles varying sizes without contiguous-space fragmentation") {
    std::array<PosePayloadBlock, 8> blocks;
    PosePayloadStore store(blocks);
    std::array<std::byte, 2040> a, b, c;
    a.fill(std::byte{0x15}); b.fill(std::byte{0x32}); c.fill(std::byte{0x67});
    const auto first = store.store(a), second = store.store(b), third = store.store(c);
    store.release(second, static_cast<unsigned>(b.size()));
    std::array<std::byte, 4080> large, result;
    large.fill(std::byte{0x91});
    REQUIRE(store.canReplace(0, static_cast<unsigned>(large.size())));
    const auto joined = store.store(large);
    REQUIRE(store.load(joined, static_cast<unsigned>(large.size()), result));
    CHECK(result == large);
    std::array<std::byte, 2040> check;
    REQUIRE(store.load(first, static_cast<unsigned>(a.size()), check)); CHECK(check == a);
    REQUIRE(store.load(third, static_cast<unsigned>(c.size()), check)); CHECK(check == c);
    CHECK(store.usage().liveAllocated == sizeof(blocks));
}

TEST_CASE("bounded pose reader buffers refuse excess leases and recover without corrupting pins") {
    std::array<PoseHistorySlot, 1> slots;
    std::array<PosePayloadBlock, 4> blocks;
    auto history = std::make_unique<PoseHistory>(slots.data(), 1, std::span{blocks});
    Input input;
    REQUIRE(history->record(input.value).status == PoseRecordStatus::Recorded);
    std::vector<PoseReadLease> readers;
    for (unsigned i = 0; i < kPoseReadBufferCount; ++i) {
        readers.push_back(history->newest()); REQUIRE(readers.back());
    }
    CHECK_FALSE(history->newest());
    CHECK(history->readFailures() == 1);
    readers.back().release();
    auto replacement = history->newest(); REQUIRE(replacement);
    CHECK(readers.front().get()->header.frameEpoch == 1);
    input.at(2); CHECK(history->record(input.value).status == PoseRecordStatus::ReaderBusy);
    replacement.release(); readers.clear();
    REQUIRE(history->record(input.value).status == PoseRecordStatus::Recorded);
}

TEST_CASE("packed history keeps the full frame window at 30 and 60 Hz within measured-shape synthetic workload") {
    const auto start = std::chrono::steady_clock::now();
    for (unsigned fps : {30u, 60u}) {
        auto slots = std::make_unique<PoseHistorySlot[]>(kHistoryCapacity);
        auto blocks = std::make_unique<PosePayloadBlock[]>(kPosePayloadBlockCount);
        auto history = std::make_unique<PoseHistory>(slots.get(), kHistoryCapacity,
            std::span{blocks.get(), kPosePayloadBlockCount});
        Input input(17, 488);
        // Synthetic bit patterns exercise compression and full roster size, not a game-derived pose.
        for (unsigned epoch = 1; epoch <= fps * 70; ++epoch) {
            input.at(epoch, fps);
            for (auto& bone : input.bones) for (unsigned j = 0; j < 16; ++j)
                bone.words[j] = j < 8 ? (j % 2 ? 0x3f800000 : 0) : 0x3f000000u + epoch * 16 + j;
            REQUIRE(history->record(input.value).status == PoseRecordStatus::Recorded);
            history->trimToWindow(kRecallWindowNanoseconds);
        }
        const unsigned expected = std::min<unsigned>(kHistoryCapacity, fps * 64 + 1);
        REQUIRE(history->count() == expected);
        auto newest = history->newest(), oldest = history->newest(expected - 1);
        REQUIRE(newest); REQUIRE(oldest);
        CHECK(newest.get()->header.elapsedNanoseconds - oldest.get()->header.elapsedNanoseconds >=
            kRecallWindowNanoseconds - 1'000'000'000 / fps - 1);
        CHECK(oldest.get()->bones[487].words[15] == 0x3f000000u + (fps * 70 - expected + 1) * 16 + 15);
        CHECK(history->storageFailures() == 0);
        CHECK(history->payloadUsage().peakAllocated <= kPosePayloadArenaBytes);
        history->clear();
        CHECK(oldest.get()->header.frameEpoch != 0);
    }
    std::cout << "packed synthetic history ms=" <<
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() << '\n';
}

TEST_CASE("72 MiB pose arena retains a full 30 Hz window with maximum incompressible frames") {
    auto slots = std::make_unique<PoseHistorySlot[]>(kHistoryCapacity);
    auto blocks = std::make_unique<PosePayloadBlock[]>(kPosePayloadBlockCount);
    auto history = std::make_unique<PoseHistory>(slots.get(), kHistoryCapacity,
        std::span{blocks.get(), kPosePayloadBlockCount});
    Input input(32, 512);
    std::uint32_t random = 123456789;
    for (unsigned epoch = 1; epoch <= 2100; ++epoch) {
        input.at(epoch, 30);
        for (auto& bone : input.bones) for (auto& word : bone.words) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            word = random;
        }
        REQUIRE(history->record(input.value).status == PoseRecordStatus::Recorded);
    }
    REQUIRE(history->count() == 1921);
    auto newest = history->newest(), oldest = history->newest(1920);
    REQUIRE(newest); REQUIRE(oldest);
    CHECK(newest.get()->header.elapsedNanoseconds - oldest.get()->header.elapsedNanoseconds == kRecallWindowNanoseconds);
    CHECK(std::memcmp(newest.get()->bones, input.bones.data(), sizeof(newest.get()->bones)) == 0);
    CHECK(history->storageFailures() == 0);
    CHECK(history->payloadUsage().peakAllocated <= kPosePayloadArenaBytes);
}
