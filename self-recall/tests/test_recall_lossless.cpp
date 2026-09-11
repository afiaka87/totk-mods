#include "RecallAppearanceHistory.hpp"
#include "RecallCorpusQueue.hpp"
#include "RecallNativeEffectSchema.hpp"
#include "doctest.h"
#include <memory>
#include <thread>
#include <vector>
using namespace self_recall::pure;

TEST_CASE("expired appearance survives a pinned pose and is released before slot reuse") {
    auto slots = std::make_unique<PoseHistorySlot[]>(8);
    std::array<PosePayloadBlock, 8> blocks;
    auto history = std::make_unique<PoseHistory>(slots.get(), 8, blocks);
    RecordedModelPose model{}; model.identity = {1, 2, 3, 0, 1, 0, 0};
    RecordedBoneMatrix bone{};
    PoseFrameInput input{}; input.models = &model; input.bones = &bone;
    input.header.modelCount = input.header.boneCount = 1;
    input.header.worldGeneration = input.header.modelGeneration = 1;
    input.header.frameEpoch = 1; input.header.elapsedNanoseconds = 1;
    const auto first = history->record(input);
    REQUIRE(first.status == PoseRecordStatus::Recorded);
    auto pinned = history->acquire(first.key); REQUIRE(pinned);
    AppearanceBlobs<4, 4, 16> blobs; blobs.initialize();
    AppearanceFrames<8, 1> frames;
    const std::array<std::byte, 16> material{std::byte{0x81}};
    const auto token = blobs.create(material);
    REQUIRE(frames.bind(blobs, first.key, std::array<unsigned, 1>{token}));
    blobs.release(token);
    input.header.frameEpoch = 2; input.header.elapsedNanoseconds = kRecallWindowNanoseconds + 2;
    REQUIRE(history->record(input).status == PoseRecordStatus::Recorded);
    const auto retired = [&](auto key) { return history->canReleaseAppearance(key); };
    CHECK_FALSE(history->contains(first.key));
    CHECK(frames.collect(blobs, retired) == 0);
    CHECK(blobs.equal(frames.token(first.key, 0), material));
    pinned.release();
    CHECK(frames.collect(blobs, retired) == 1);
    CHECK(frames.token(first.key, 0) == 0);
    CHECK(blobs.availableBytes() == 64);
    CHECK(frames.collect(blobs, retired) == 0);
}

TEST_CASE("packed schemas own only used names definitions and enums without changing their values") {
    using namespace self_recall::equipment_effects::detail;
    auto full = std::make_unique<NativeEffectSchema>();
    alignas(8) std::byte instance[0x60]{}, user[0x50]{}, enumeration[112]{}, scalar[112]{};
    char userName[] = "GlowBlade", name[] = "State", first[] = "Drawn", second[] = "Sheathed";
    EnumValue choices[]{{first, 1, 0}, {second, 7, 0}};
    const void* definitions[]{enumeration, scalar};
    write<const void*>(instance, 0x58, user);
    write<const void*>(user, 0x10, userName);
    write<std::uint16_t>(user, 0x44, 2);
    write<const void*>(user, 0x48, definitions);
    write<const void*>(enumeration, 8, name);
    write<int>(enumeration, 96, 2); write<int>(enumeration, 100, 500);
    write<const void*>(enumeration, 104, choices);
    write<const char*>(scalar, 8, "Glow"); write<unsigned>(scalar, 0x58, 2);
    write<unsigned>(scalar, 96, 0x80000000); write<unsigned>(scalar, 100, 0x7fc01234);
    const auto size = measureSchema(instance); REQUIRE(size);
    CHECK(size.properties == 2); CHECK(size.enums == 2);
    CHECK(size.names == sizeof(userName) + sizeof(first) + sizeof(second));
    CHECK(size.bytes() < 512);
    std::vector<std::uint64_t> storage((size.bytes() + 7) / 8 + 1, UINT64_MAX);
    PackedEffectSchema compact;
    REQUIRE(compact.bind(std::as_writable_bytes(std::span{storage}), size));
    REQUIRE(copySchema(*full, instance)); REQUIRE(copySchema(compact, instance));
    CHECK(storage.back() == UINT64_MAX);
    std::memset(userName, 'X', sizeof(userName) - 1);
    std::memset(first, 'Y', sizeof(first) - 1);
    CHECK(std::strcmp(compact.userName, full->userName) == 0);
    CHECK(std::strcmp(compact.enums[0].name, full->enums[0].name) == 0);
    CHECK(compact.enums[1].value == full->enums[1].value);
    CHECK(read<unsigned>(compact.definitions[1], 96) == 0x80000000);
    CHECK(read<unsigned>(compact.definitions[1], 100) == 0x7fc01234);
    CHECK(read<unsigned>(compact.definitions[0], 100) == 2);
    CHECK(read<const void*>(compact.definitions[0], 104) == compact.enums);
    CHECK(read<const void*>(compact.table, 32) == compact.definitionPointers);
    CHECK_FALSE(compact.bind(std::as_writable_bytes(std::span{storage}).first(size.bytes() - 1), size));
    write<std::uint16_t>(user, 0x44, 3);
    CHECK_FALSE(copySchema(compact, instance));
    CHECK_FALSE(measureSchema(nullptr));
}

TEST_CASE("corpus queue never overwrites an unread record and rejects oversized submissions") {
    CorpusQueue<2, 32> queue;
    std::array<std::byte, 9> data{std::byte{1}, std::byte{255}};
    const std::array<std::span<const std::byte>, 1> parts{data};
    REQUIRE(queue.push(CorpusKind::Pose, 42, parts));
    REQUIRE(queue.push(CorpusKind::Appearance, 43, parts));
    CHECK_FALSE(queue.push(CorpusKind::Pose, 44, parts));
    const auto* first = queue.front(); REQUIRE(first);
    CHECK(first->header.time == 42); CHECK(first->header.sequence == 1);
    CHECK(first->header.checksum == corpusChecksum(data));
    CHECK(std::memcmp(first->data.data(), data.data(), data.size()) == 0);
    queue.pop(); REQUIRE(queue.push(CorpusKind::Binding, 45, parts));
    CHECK(queue.front()->header.time == 43); queue.pop();
    CHECK(queue.front()->header.time == 45); queue.pop(); CHECK(queue.front() == nullptr);
    const std::array<std::byte, 33> tooLarge{};
    CHECK_FALSE(queue.push(CorpusKind::Pose, 0, std::array<std::span<const std::byte>, 1>{tooLarge}));
    CHECK(queue.dropped() == 2);
}

TEST_CASE("corpus writer sees only complete published records under concurrent recording") {
    CorpusQueue<4, 256> queue;
    std::atomic<bool> done{false};
    std::atomic<unsigned> failures{0}, read{0};
    std::thread writer([&] {
        while (!done.load() || queue.front()) {
            const auto* record = queue.front();
            if (!record) { std::this_thread::yield(); continue; }
            if (record->header.checksum != corpusChecksum({record->data.data(), record->header.bytes}) ||
                record->data[0] != std::byte(record->header.time & 255)) ++failures;
            ++read; queue.pop();
        }
    });
    unsigned submitted = 0;
    for (unsigned i = 1; i <= 10000; ++i) {
        std::array<std::byte, 256> payload; payload.fill(std::byte(i & 255));
        submitted += queue.push(CorpusKind::Pose, i, std::array<std::span<const std::byte>, 1>{payload});
    }
    done.store(true); writer.join();
    CHECK(failures.load() == 0); CHECK(read.load() == submitted);
    CHECK(read.load() + queue.dropped() == 10000);
}
