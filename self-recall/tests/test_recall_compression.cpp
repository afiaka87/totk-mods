#include <doctest.h>
#include "RecallPoseHistory.hpp"
#include "RecallCompressedAppearance.hpp"
#include <memory>
#include <vector>
#include <random>
#include <thread>
using namespace self_recall::pure;

namespace {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif
struct Fixture {
    std::array<PoseHistorySlot, 16> slots{};
    std::array<PosePayloadBlock, 128> blocks{};
    PoseHistory history{slots.data(), unsigned(slots.size()), blocks};
    RecordedModelPose model{};
    RecordedBoneMatrix bone{};
    PoseFrameInput input{};
    Fixture() {
        model.identity = {1,2,3,0,1,0,1};
        input.models = &model; input.bones = &bone;
        input.header.modelCount = input.header.boneCount = input.header.materialCount = 1;
        input.header.worldGeneration = input.header.modelGeneration = 1;
    }
    PoseRecordReport record(unsigned serial) {
        input.header.frameEpoch = serial;
        input.header.elapsedNanoseconds = std::uint64_t(serial) * 1'000'000'000 / 30;
        bone.words[0] = serial;
        bone.words[1] = 0x80000000;
        bone.words[2] = 0x7fc12345;
        return history.record(input);
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
    CHECK(f->history.payloadUsage().liveAllocated == 0);
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
    CHECK(store.usage().liveAllocated == 6 * 1024);
    CHECK(store.load(a, unsigned(raw.size()), raw));
    store.release(c, unsigned(raw.size()));
    CHECK(store.usage().liveAllocated == 0);
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
    CHECK(f->history.payloadUsage().liveAllocated == 0);
}

TEST_CASE("static pose codec preserves incompressible IEEE words and rejects damaged data") {
    auto encoder = std::make_unique<PoseCompressor>();
    auto decoder = std::make_unique<PoseDecompressor>();
    std::array<std::byte, 35000> raw{}, packed{}, decoded{};
    std::mt19937 random(9917);
    for (auto& byte : raw) byte = std::byte(random() & 255);
    CHECK(encoder->compress(raw, packed) == 0); // Expansion uses the caller's raw fallback.
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
    CHECK(history->payloadUsage().liveAllocated == 0);
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
