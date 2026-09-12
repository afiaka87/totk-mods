#include <atomic>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include <doctest.h>

#include "PoseTestSupport.hpp"
#include "RecallAppearance.hpp"
#include "RecallBase.hpp"
#include "RecallPlayback.hpp"
#include "RecallPoseData.hpp"

namespace self_recall_tests::test_recall_pose_history {
using namespace self_recall::pure;

namespace {
struct Fixture : PoseTestInput {
    PoseTestStorage storage{4, 4 * 36};
    PoseHistory& history = storage.history;
    PoseFrameInput& input = value;

    Fixture() : PoseTestInput(2, 3) {
        models[0].identity = {10, 20, 30, 0, 1, 0, 0};
        models[1].identity = {11, 21, 31, 1, 2, 0, 0};
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
}

namespace self_recall_tests::test_recall_pose_payload {
using namespace self_recall::pure;

namespace {
struct Input : PoseTestInput {
    Input(unsigned modelCount = 1, unsigned boneCount = 1)
        : PoseTestInput(modelCount, boneCount, kPoseMaterialLimit) {
        auto& h = value.header;
        h.route.flags = SampleAdmissible;
        for (unsigned i = 0; i < modelCount; ++i) {
            auto& m = models[i];
            m.visibility = i; m.queueAdmission = i % 2;
        }
        for (unsigned i = 0; i < 16; ++i) {
            value.visible.bones[i] = 0xf0f00000u + i;
            value.visible.materials[i] = 0xcccc0000u + i;
        }
    }
};
}

TEST_CASE("route headers remain available with every pose decoder pinned and reject stale keys") {
    std::array<PoseHistorySlot, 8> slots;
    std::array<PosePayloadBlock, 32> blocks;
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
    for (unsigned back = 0; back < 8; ++back) {
        PoseFrameHeader header;
        REQUIRE(history->copyHeaderBefore(anchor, back, header));
        CHECK(header.frameEpoch == 8 - back);
        CHECK(header.route.pose.position.x == float(8 - back));
        CHECK(history->contains(header.key));
    }
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
    readers.back().release();
    auto replacement = history->newest(); REQUIRE(replacement);
    CHECK(readers.front().get()->header.frameEpoch == 1);
    input.at(2); CHECK(history->record(input.value).status == PoseRecordStatus::ReaderBusy);
    replacement.release(); readers.clear();
    REQUIRE(history->record(input.value).status == PoseRecordStatus::Recorded);
}

}

namespace self_recall_tests::test_recall_compression {
using namespace self_recall::pure;

namespace {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif
struct Fixture : PoseTestInput {
    PoseTestStorage storage{16, 128};
    PoseHistory& history = storage.history;

    Fixture() : PoseTestInput(1, 1, 1) {
        models[0].identity = {1, 2, 3, 0, 1, 0, 1};
    }
    PoseRecordReport record(unsigned serial) {
        at(serial, 30);
        bones[0].words[0] = serial;
        bones[0].words[1] = 0x80000000;
        bones[0].words[2] = 0x7fc12345;
        return history.record(value);
    }
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

TEST_CASE("compressed history survives parent expiry and generation changes with pinned readers") {
    auto f = std::make_unique<Fixture>();
    std::vector<PoseFrameKey> keys;
    for (unsigned i = 1; i <= 16; ++i) {
        const auto result = f->record(i); REQUIRE(result.status == PoseRecordStatus::Recorded);
        keys.push_back(result.key);
    }
    auto pinned = f->history.acquire(keys[7]); REQUIRE(pinned);
    for (unsigned i = 17; i <= 23; ++i) REQUIRE(f->record(i).status == PoseRecordStatus::Recorded);
    for (unsigned i = 8; i < keys.size(); ++i) {
        auto pose = f->history.acquire(keys[i]); REQUIRE(pose);
        CHECK(pose.get()->bones[0].words[0] == i + 1);
        CHECK(pose.get()->bones[0].words[1] == 0x80000000);
        CHECK(pose.get()->bones[0].words[2] == 0x7fc12345);
    }
    f->history.clear();
    CHECK(pinned.get()->bones[0].words[0] == 8);
    CHECK_FALSE(f->history.acquire(keys[7]));
    pinned.release();
    f->history.clear();
    REQUIRE(f->record(40).status == PoseRecordStatus::Recorded);
    CHECK(f->history.newest().get()->bones[0].words[0] == 40);
}

TEST_CASE("compressed block dependencies hold until the last owning root is released") {
    std::array<PosePayloadBlock, 12> blocks{};
    PosePayloadStore store(blocks);
    std::array<std::byte, 1050> raw{};
    const auto a = store.store(raw);
    const auto b = store.store(raw, a, unsigned(raw.size()));
    const auto c = store.store(raw, b, unsigned(raw.size()));
    store.release(a, unsigned(raw.size())); store.release(b, unsigned(raw.size()));
    CHECK(store.load(a, unsigned(raw.size()), raw));
    store.release(c, unsigned(raw.size()));
    std::array<unsigned, 6> reused{};
    for (auto& block : reused) {
        block = store.store(raw);
        REQUIRE(block != UINT32_MAX);
    }
    CHECK(store.store(raw) == UINT32_MAX);
    for (auto block : reused) store.release(block, unsigned(raw.size()));
}

TEST_CASE("compressed readers race recording and clearing without torn poses") {
    auto f = std::make_unique<Fixture>();
    REQUIRE(f->record(1).status == PoseRecordStatus::Recorded);
    std::atomic<bool> done{false}, failed{false};
    std::atomic<unsigned> reads{0};
    std::vector<std::thread> readers;
    for (unsigned i = 0; i < 4; ++i) readers.emplace_back([&] {
        while (!done.load()) {
            auto pose = f->history.newest();
            if (!pose) continue;
            const auto& frame = *pose.get();
            if (frame.bones[0].words[0] != frame.header.frameEpoch ||
                frame.bones[0].words[1] != 0x80000000 ||
                frame.bones[0].words[2] != 0x7fc12345) failed.store(true);
            ++reads;
        }
    });
    for (unsigned i = 2; i <= 10000; ++i) {
        if (i % 1000 == 0) f->history.clear();
        const auto status = f->record(i).status;
        if (status != PoseRecordStatus::Recorded && status != PoseRecordStatus::ReaderBusy)
            failed.store(true);
    }
    done.store(true);
    for (auto& reader : readers) reader.join();
    CHECK_FALSE(failed.load());
    CHECK(reads.load() > 0);
    f->history.clear();
}

TEST_CASE("static pose codec preserves incompressible IEEE words and rejects damaged data") {
    auto encoder = std::make_unique<PoseCompressor>();
    auto decoder = std::make_unique<PoseDecompressor>();
    std::array<std::byte, 35000> raw{}, packed{}, decoded{};
    std::mt19937 random(9917);
    for (auto& byte : raw) byte = std::byte(random() & 255);
    CHECK(encoder->compress(raw, packed) == 0);
    for (unsigned i = 0; i < raw.size(); ++i) raw[i] = std::byte(i % 251);
    const auto size = encoder->compress(raw, packed); REQUIRE(size);
    REQUIRE(decoder->decompress({packed.data(), size}, decoded));
    CHECK(raw == decoded);
    CHECK_FALSE(decoder->decompress({packed.data(), size - 1}, decoded));
}

TEST_CASE("maximum roster and incompressible bones survive compressed history rollover") {
    auto slots = std::make_unique<PoseHistorySlot[]>(16);
    auto blocks = std::make_unique<PosePayloadBlock[]>(1024);
    auto history = std::make_unique<PoseHistory>(slots.get(), 16, std::span{blocks.get(), 1024});
    auto frame = std::make_unique<RecordedPoseFrame>();
    frame->header.modelCount = kPoseModelLimit;
    frame->header.boneCount = kPoseBoneLimit;
    frame->header.worldGeneration = frame->header.modelGeneration = 1;
    for (unsigned i = 0; i < kPoseModelLimit; ++i)
        frame->models[i].identity = {i+1, i+100, i+200,
            static_cast<std::uint16_t>(i*16), 16, 0, 0};
    std::mt19937 random(13337);
    for (unsigned i = 1; i <= 40; ++i) {
        frame->header.frameEpoch = i;
        frame->header.elapsedNanoseconds = std::uint64_t(i) * 1'000'000'000 / 30;
        for (auto& bone : frame->bones) for (auto& word : bone.words) word = random();
        auto result = history->record({frame->header, frame->models, frame->bones, frame->visible});
        REQUIRE(result.status == PoseRecordStatus::Recorded);
        auto pose = history->acquire(result.key); REQUIRE(pose);
        CHECK(std::memcmp(pose.get()->bones, frame->bones, sizeof(frame->bones)) == 0);
        CHECK(std::memcmp(pose.get()->models, frame->models, sizeof(frame->models)) == 0);
    }
    history->clear();
}

TEST_CASE("compressed appearance reconstructs every byte across LZ4 overlap and raw fallback") {
    auto blobs = std::make_unique<CompressedAppearanceBlobs<600,600>>();
    blobs->initialize();
    std::mt19937 random(76123);
    for (unsigned n : {1u,15u,255u,256u,1024u,65536u}) {
        for (unsigned mode = 0; mode < 3; ++mode) {
            std::vector<std::byte> source(n), output(n);
            for (unsigned i = 0; i < n; ++i)
                source[i] = std::byte(mode == 0 ? random() & 255 : mode == 1 ? i % 7 : 0);
            const auto token = blobs->create(source); REQUIRE(token);
            CHECK(blobs->size(token) == n);
            CHECK(blobs->equal(token, source));
            REQUIRE(blobs->copy(token, output)); CHECK(source == output);
            source.back() ^= std::byte{1}; CHECK_FALSE(blobs->equal(token, source));
            CHECK(blobs->retain(token)); blobs->release(token);
            REQUIRE(blobs->copy(token, output));
            blobs->release(token); CHECK(blobs->size(token) == 0);
        }
    }
    CHECK(blobs->availableBytes() == 600 * 256);
}
}

namespace self_recall_tests::test_recall_pose_transport {
using namespace self_recall::pure;

namespace {
RecordedBoneMatrix fixture() {
    const float columns[16] = {0, 2, 0, 0, -3, 0, 0, 0, 0, 0, 4, 0, 12, 34, 56, 0};
    RecordedBoneMatrix bone;
    std::memcpy(bone.words, columns, sizeof(columns));
    for (unsigned i = 3; i < 16; i += 4) bone.words[i] = 0x7fc00000u + i;
    return bone;
}
}
}

namespace self_recall_tests::test_recall_format {
using namespace self_recall::pure;

TEST_CASE("recorded route sample ABI is frozen") {
    CHECK(sizeof(Pose) == 48);
    CHECK(sizeof(HistorySample) == 68);
    CHECK(offsetof(HistorySample, pose) == 0);
    CHECK(offsetof(HistorySample, engineVelocity) == 48);
    CHECK(offsetof(HistorySample, pathSpeed) == 60);
    CHECK(offsetof(HistorySample, flags) == 64);
    CHECK(offsetof(Pose, rotation) == 0);
    CHECK(offsetof(Pose, position) == 36);
    CHECK(SampleAdmissible == 1u);
    CHECK(SampleClimb == 2u);
}
}

namespace self_recall_tests::test_recall_game_time {
using namespace self_recall::pure;

TEST_CASE("paused model frames cannot advance gameplay time") {
    GameTime clock;
    auto before = clock.update(0.5f, false);
    bool stayedPaused = true;
    for (unsigned i = 0; i < 10000; ++i) {
        const auto paused = clock.update(1.0f, true);
        stayedPaused &= paused.status == GameTimeStatus::Paused &&
                        paused.elapsedNanoseconds == before.elapsedNanoseconds;
    }
    CHECK(stayedPaused);
    auto after = clock.update(0.5f, false);
    CHECK(after.serial == before.serial + 10001);
    CHECK(after.elapsedNanoseconds == 33333333ull);
    after = clock.update(0.5f, false);
    CHECK(after.elapsedNanoseconds == 50000000ull);
}

TEST_CASE("invalid or unavailable clock updates preserve accumulated time") {
    GameTime clock;
    const auto start = clock.update(1.0f, false);
    for (const auto value : {-1.0f, 0.25f, std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity()}) {
        const auto invalid = clock.update(value, false);
        CHECK(invalid.status == GameTimeStatus::InvalidDelta);
        CHECK(invalid.elapsedNanoseconds == start.elapsedNanoseconds);
    }
    const auto missing = clock.update(1.0f, false, false);
    CHECK(missing.status == GameTimeStatus::Unavailable);
    CHECK(missing.elapsedNanoseconds == start.elapsedNanoseconds);
    const auto zero = clock.update(0.0f, false);
    CHECK(zero.status == GameTimeStatus::NoAdvance);
    CHECK(zero.elapsedNanoseconds == start.elapsedNanoseconds);
    CHECK(clock.update(1.0f, false).elapsedNanoseconds == 66666666ull);
}

TEST_CASE("history expires by elapsed duration and preserves existing readers") {
    for (const unsigned fps : {30u, 60u}) {
        PoseTestStorage storage{256, 256 * 36};
        auto& history = storage.history;
        PoseTestInput source;
        auto& input = source.value;
        GameTime clock;
        PoseReadLease oldReader;
        PoseFrameKey firstKey;
        for (unsigned i = 0; i < 3 * fps; ++i) {
            const auto time = clock.update(fps == 60 ? 0.5f : 1.0f, false);
            input.header.frameEpoch = time.serial;
            input.header.elapsedNanoseconds = time.elapsedNanoseconds;
            const auto stored = history.record(input);
            REQUIRE(stored.status == PoseRecordStatus::Recorded);
            if (i == 0) {
                firstKey = stored.key;
                oldReader = history.acquire(firstKey);
            }
            history.trimToWindow(2000000000ull);
        }
        REQUIRE(oldReader);
        CHECK(oldReader.get()->header.key == firstKey);
        CHECK_FALSE(history.acquire(firstKey));
        CHECK(history.count() == 2 * fps + 1);
        auto newest = history.newest();
        auto oldest = history.newest(history.count() - 1);
        REQUIRE(newest);
        REQUIRE(oldest);
        CHECK(newest.get()->header.elapsedNanoseconds -
              oldest.get()->header.elapsedNanoseconds == 2000000000ull);
        CHECK_FALSE(history.newest(history.count()));
    }
}

TEST_CASE("exhausted gameplay time never wraps or resumes from a lost interval") {
    GameTime clock;
    GameTimeSnapshot sample;
    for (unsigned i = 0; i < 1000; ++i) {
        sample = clock.update(1000000000.0f, false);
        if (sample.status == GameTimeStatus::Exhausted) break;
    }
    REQUIRE(sample.status == GameTimeStatus::Exhausted);
    const auto later = clock.update(0.5f, false);
    CHECK(later.status == GameTimeStatus::Exhausted);
    CHECK(later.elapsedNanoseconds == sample.elapsedNanoseconds);
}
}

namespace self_recall_tests::test_recall_frame_ticket {
using namespace self_recall::pure;

TEST_CASE("pose ticket rejects recycled actors and mismatched movement") {
    ActorFrameTicket ticket{};
    ticket.actor = 100;
    ticket.actorId = 7;
    ticket.model = 200;
    ticket.worldGeneration = 3;
    ticket.route.pose.rotation.values[0] = 1;
    ticket.route.pose.rotation.values[4] = 1;
    ticket.route.pose.rotation.values[8] = 1;
    ticket.route.pose.position = {3, 4, 5};
    ticket.modelRoot[0] = ticket.modelRoot[5] = ticket.modelRoot[10] = 1;
    ticket.modelRoot[3] = 3;
    ticket.modelRoot[7] = 4;
    ticket.modelRoot[11] = 5;
    const auto matches = [&](const ActorFrameTicket& current) {
        return matchesActorFrame(ticket, current.actor, current.actorId, current.model,
                                 current.worldGeneration, current.route.pose, current.modelRoot);
    };
    CHECK(matches(ticket));
    auto changed = ticket;
    ++changed.actorId;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    ++changed.worldGeneration;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    ++changed.model;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    changed.route.pose.position.x += 1;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    changed.route.pose.rotation.values[0] = -1;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    changed.modelRoot[7] += 1;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    changed.modelRoot[5] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(matches(changed));
    CHECK_FALSE(matchesActorFrame(ticket, 100, 7, 200, 3, ticket.route.pose, nullptr));
}

TEST_CASE("frame mailbox never mixes concurrently published frame fields") {
    struct Frame { std::uint64_t serial = 0; std::uint64_t fields[24]{}; };
    FrameMailbox<Frame> mailbox;
    std::atomic<bool> done{false};
    std::atomic<unsigned> incoherent{0};
    std::atomic<unsigned> observed{0};
    std::thread reader([&] {
        do {
            Frame frame;
            if (!mailbox.snapshot(frame) || !frame.serial) continue;
            for (const auto value : frame.fields)
                if (value != frame.serial) ++incoherent;
            ++observed;
        } while (!done.load(std::memory_order_acquire));
    });
    for (std::uint64_t i = 1; i <= 10000; ++i) {
        Frame frame;
        frame.serial = i;
        for (auto& value : frame.fields) value = i;
        while (!mailbox.publish(frame)) std::this_thread::yield();
    }
    while (!observed.load()) std::this_thread::yield();
    done.store(true, std::memory_order_release);
    reader.join();
    CHECK(incoherent.load() == 0);
    Frame finalFrame;
    REQUIRE(mailbox.snapshot(finalFrame));
    CHECK(finalFrame.serial == 10000);
}
}

namespace self_recall_tests::test_recall_pose_transport {
using namespace self_recall::pure;

namespace {
std::array<float, 3> transform(const float matrix[12], const std::array<float, 3>& point) {
    std::array<float, 3> result{};
    for (unsigned row = 0; row < 3; ++row) {
        result[row] = matrix[row * 4 + 3];
        for (unsigned col = 0; col < 3; ++col) result[row] += matrix[row * 4 + col] * point[col];
    }
    return result;
}
}

TEST_CASE("native padded bone columns produce the historical world wrist matrix") {
    const auto bone = fixture();
    RecordedModelPose model{{100, 200, 300}, 0, 1};
    float world[12]{};
    REQUIRE(boneToWorldMatrix(bone, model, world));
    const float expected[12] = {0, -3, 0, 112, 2, 0, 0, 234, 0, 0, 4, 356};
    for (unsigned i = 0; i < 12; ++i) CHECK(world[i] == expected[i]);
    const auto point = transform(world, {1, 2, 3});
    CHECK(point == std::array<float, 3>{106, 236, 368});
}

TEST_CASE("render-origin changes preserve historical world geometry") {
    const auto bone = fixture();
    for (std::uint32_t fromRelative : {0u, 1u}) {
        for (std::uint32_t toRelative : {0u, 1u}) {
            RecordedModelPose from{{100, 200, 300}, 0, fromRelative};
            RecordedModelPose to{{-1000, 500, 4096}, 0, toRelative};
            RecordedBoneMatrix rebased;
            REQUIRE(rebaseBoneForRender(bone, from, to, rebased));
            float before[12], after[12];
            REQUIRE(boneToWorldMatrix(bone, from, before));
            REQUIRE(boneToWorldMatrix(rebased, to, after));
            for (const auto& point : {std::array<float, 3>{0, 0, 0}, {1, 2, 3}, {-7, 2, -5}})
                CHECK(transform(before, point) == transform(after, point));
            for (unsigned word = 0; word < 16; ++word)
                if (word < 12 || word == 15) CHECK(rebased.words[word] == bone.words[word]);
        }
    }
}

TEST_CASE("unchanged render origins preserve translation bits and opaque padding") {
    auto bone = fixture();
    bone.words[12] = 0x80000000u;
    RecordedModelPose model{{1.0e20f, -1.0e20f, 1.0e20f}, 0, 1};
    RecordedBoneMatrix copy;
    REQUIRE(rebaseBoneForRender(bone, model, model, copy));
    CHECK(std::memcmp(&bone, &copy, sizeof(bone)) == 0);
}

TEST_CASE("nonfinite transforms fail without publishing partial output") {
    auto bone = fixture();
    RecordedModelPose model{};
    float world[12];
    for (auto& value : world) value = -77;
    bone.words[4] = 0x7f800000u;
    CHECK_FALSE(boneToWorldMatrix(bone, model, world));
    for (float value : world) CHECK(value == -77);
    bone = fixture();
    RecordedBoneMatrix destination = bone;
    model.renderOrigin[1] = std::numeric_limits<float>::infinity();
    model.originRelative = 1;
    CHECK_FALSE(rebaseBoneForRender(bone, {}, model, destination));
    CHECK(std::memcmp(&bone, &destination, sizeof(bone)) == 0);
    model.originRelative = 0;
    CHECK(rebaseBoneForRender(bone, {}, model, destination));
}
}
