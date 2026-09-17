#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstring>
#include <memory>
#include <vector>

#include <doctest.h>

#include "PoseTestSupport.hpp"
#include "RecallBase.hpp"
#include "RecallPoseData.hpp"

namespace self_recall_tests::test_spill {
using namespace self_recall::pure;

namespace {

class MemoryFile final : public SpillFile {
public:
    std::vector<std::byte> bytes = std::vector<std::byte>(kSpillFileBytes);
    bool failWrites = false;
    bool failReads = false;
    std::uint64_t clock = 0;

    bool write(std::uint64_t offset, std::span<const std::byte> data) override {
        if (failWrites || offset + data.size() > bytes.size()) return false;
        std::memcpy(bytes.data() + offset, data.data(), data.size());
        return true;
    }
    bool read(std::uint64_t offset, std::span<std::byte> data) override {
        if (failReads || offset + data.size() > bytes.size()) return false;
        std::memcpy(data.data(), bytes.data() + offset, data.size());
        return true;
    }
    std::uint64_t nanoseconds() override { return clock += 1000; }
};

constexpr unsigned kFps = 30;
constexpr std::uint64_t kFrameNs = 1'000'000'000ull / kFps;

struct Rig {
    std::unique_ptr<PoseHistorySlot[]> slots = std::make_unique<PoseHistorySlot[]>(kHistoryCapacity);
    std::unique_ptr<PosePayloadBlock[]> blocks;
    std::unique_ptr<PosePayloadBlock[]> cacheBlocks;
    std::unique_ptr<SpillGroup[]> groups = std::make_unique<SpillGroup[]>(kHistoryCapacity);
    std::vector<std::byte> ring = std::vector<std::byte>(kSpillWriteRingBytes);
    std::vector<std::byte> io = std::vector<std::byte>(kSpillMaxGroupBytes);
    std::unique_ptr<PoseSpill> spill;
    std::unique_ptr<PoseHistory> history;
    MemoryFile file;
    PoseTestInput input{1, 8};
    std::vector<std::uint32_t> seeds;

    Rig(unsigned poolBlocks, unsigned cacheBlockCount, std::uint64_t ramNs)
        : blocks(std::make_unique<PosePayloadBlock[]>(poolBlocks)),
          cacheBlocks(std::make_unique<PosePayloadBlock[]>(cacheBlockCount)) {
        spill = std::make_unique<PoseSpill>(std::span{cacheBlocks.get(), cacheBlockCount},
                                            std::span{groups.get(), kHistoryCapacity}, ring, io);
        spill->setEnabled(true);
        history = std::make_unique<PoseHistory>(slots.get(), kHistoryCapacity,
                                                std::span{blocks.get(), poolBlocks}, spill.get(), ramNs);
    }

    // Frame content derives from the epoch so any decoded frame can be checked exactly.
    static void fill(PoseTestInput& target, unsigned epoch) {
        std::uint32_t state = epoch * 2654435761u + 12345u;
        for (auto& bone : target.bones)
            for (auto& word : bone.words) {
                state = state * 1664525u + 1013904223u;
                word = state;
            }
        target.value.header.frameEpoch = epoch;
        target.value.header.elapsedNanoseconds = std::uint64_t{epoch} * kFrameNs;
        target.value.header.route.pose.position.x = static_cast<float>(epoch);
    }

    PoseRecordReport record(unsigned epoch) {
        fill(input, epoch);
        return history->record(input.value);
    }

    unsigned pumpAll() {
        unsigned operations = 0;
        while (spill->pump(file)) ++operations;
        return operations;
    }

    static bool matches(const RecordedPoseFrame& frame, unsigned epoch) {
        PoseTestInput expected{1, 8};
        fill(expected, epoch);
        return frame.header.frameEpoch == epoch &&
               std::memcmp(frame.bones, expected.bones.data(), 8 * sizeof(RecordedBoneMatrix)) == 0;
    }

    // Walks the whole history newest to oldest the way Recall does, loading from the file.
    unsigned recallWalk(unsigned maxPumpsPerFrame = 64) {
        auto newest = history->newest();
        REQUIRE(newest);
        const auto anchor = newest.get()->header.key;
        newest.release();
        const auto count = history->count();
        spill->setPlayback(true, anchor.generation, anchor.serial);
        unsigned exact = 0;
        for (std::uint32_t index = 0; index < count; ++index) {
            unsigned pumps = 0;
            for (;;) {
                const auto probe = history->spillAvailableThrough(anchor, index, index);
                REQUIRE(probe.blocked != SpillAvailability::Lost);
                if (probe.through == index && probe.blocked == SpillAvailability::Available) break;
                REQUIRE(pumps++ < maxPumpsPerFrame);
                (void)spill->pump(file);
            }
            auto lease = history->before(anchor, index);
            REQUIRE(lease);
            const auto epoch = static_cast<unsigned>(lease.get()->header.frameEpoch);
            REQUIRE(matches(*lease.get(), epoch));
            spill->setPlayback(true, anchor.generation, lease.get()->header.key.serial);
            ++exact;
        }
        spill->setPlayback(false, 0, 0);
        pumpAll();
        return exact;
    }
};

}

TEST_CASE("SD history keeps 64 seconds with a small RAM pool and decodes every frame exactly") {
    Rig rig(700, 512, 10ull * 1'000'000'000ull);
    for (unsigned epoch = 1; epoch <= 70 * kFps; ++epoch) {
        REQUIRE(rig.record(epoch).status == PoseRecordStatus::Recorded);
        rig.pumpAll();
    }
    CHECK(rig.history->count() >= 63 * kFps);
    CHECK(rig.spill->stats().writes.load() > 0);
    CHECK(rig.spill->stats().released.load() > 0);
    CHECK(rig.spill->stats().lost.load() == 0);
    CHECK(rig.spill->stats().trimmedForSpace.load() == 0);
    // Only the recent window stays in RAM.
    CHECK(rig.history->spillAvailableThrough(rig.history->newest().get()->header.key, 0,
                                             rig.history->count() - 1).blocked == SpillAvailability::Loading);
    CHECK(rig.recallWalk() == rig.history->count());
    CHECK(rig.spill->stats().cacheBlocksUsed.load() == 0);
    CHECK(rig.spill->stats().reads.load() > 0);
}

TEST_CASE("playback waits at the loaded boundary and a tiny cache still reaches the oldest frame") {
    Rig rig(700, 96, 10ull * 1'000'000'000ull);
    for (unsigned epoch = 1; epoch <= 66 * kFps; ++epoch) {
        REQUIRE(rig.record(epoch).status == PoseRecordStatus::Recorded);
        rig.pumpAll();
    }
    const auto anchor = rig.history->newest().get()->header.key;
    rig.spill->setPlayback(true, anchor.generation, anchor.serial);
    const auto before = rig.history->spillAvailableThrough(anchor, 0, rig.history->count() - 1);
    CHECK(before.blocked == SpillAvailability::Loading);
    CHECK(before.through < rig.history->count() - 1);
    CHECK_FALSE(rig.history->before(anchor, before.through + 1));
    CHECK(rig.history->before(anchor, before.through));
    CHECK(rig.recallWalk(256) == rig.history->count());
}

TEST_CASE("write failures keep recording in RAM and expire old frames instead of refusing") {
    Rig rig(400, 256, 10ull * 1'000'000'000ull);
    rig.file.failWrites = true;
    for (unsigned epoch = 1; epoch <= 40 * kFps; ++epoch) {
        REQUIRE(rig.record(epoch).status == PoseRecordStatus::Recorded);
        rig.pumpAll();
    }
    CHECK(rig.spill->stats().writes.load() == 0);
    CHECK(rig.spill->stats().failures.load() > 0);
    CHECK(rig.spill->stats().trimmedForSpace.load() > 0);
    CHECK(rig.history->count() < 40 * kFps);
    CHECK(rig.recallWalk() == rig.history->count());
}

TEST_CASE("a disabled SD tier behaves as a RAM-only ring") {
    Rig rig(400, 256, 10ull * 1'000'000'000ull);
    rig.spill->setEnabled(false);
    for (unsigned epoch = 1; epoch <= 40 * kFps; ++epoch) {
        REQUIRE(rig.record(epoch).status == PoseRecordStatus::Recorded);
        CHECK_FALSE(rig.spill->pump(rig.file));
    }
    CHECK(rig.spill->stats().ramOnly.load() > 0);
    CHECK(rig.recallWalk() == rig.history->count());
}

TEST_CASE("a stalled card rejects new frames instead of discarding SD history, then catches up") {
    Rig rig(700, 512, 10ull * 1'000'000'000ull);
    unsigned epoch = 1;
    for (; epoch <= 64 * kFps; ++epoch) {
        REQUIRE(rig.record(epoch).status == PoseRecordStatus::Recorded);
        rig.pumpAll();
    }
    const auto countBefore = rig.history->count();
    unsigned rejected = 0;
    for (unsigned i = 0; i < 40 * kFps; ++i, ++epoch) {
        const auto status = rig.record(epoch).status;
        REQUIRE((status == PoseRecordStatus::Recorded || status == PoseRecordStatus::StorageFull));
        rejected += status == PoseRecordStatus::StorageFull;
    }
    CHECK(rejected > 0);
    CHECK(rig.spill->stats().trimmedForSpace.load() == 0);
    // History still ages by game time during the stall, but nothing older is discarded for space.
    const auto newest = rig.history->newest().get()->header;
    PoseFrameHeader oldest;
    REQUIRE(rig.history->copyHeaderBefore(newest.key, rig.history->count() - 1, oldest));
    const auto lastAttempt = std::uint64_t{epoch - 1} * kFrameNs;
    CHECK(oldest.elapsedNanoseconds >= lastAttempt - kRecallWindowNanoseconds);
    CHECK(oldest.elapsedNanoseconds <= lastAttempt - kRecallWindowNanoseconds + kFrameNs);
    CHECK(rig.history->count() < countBefore);
    CHECK(rig.history->spillAvailableThrough(newest.key, 0, rig.history->count() - 1).blocked !=
          SpillAvailability::Lost);
    rig.pumpAll();
    for (unsigned i = 0; i < 5 * kFps; ++i, ++epoch) {
        REQUIRE(rig.record(epoch).status == PoseRecordStatus::Recorded);
        rig.pumpAll();
    }
    CHECK(rig.recallWalk() == rig.history->count());
}

TEST_CASE("clearing history discards queued writes and invalidates SD frames") {
    Rig rig(700, 512, 10ull * 1'000'000'000ull);
    for (unsigned epoch = 1; epoch <= 20 * kFps; ++epoch) {
        REQUIRE(rig.record(epoch).status == PoseRecordStatus::Recorded);
        rig.pumpAll();
    }
    for (unsigned epoch = 20 * kFps + 1; epoch <= 21 * kFps; ++epoch)
        REQUIRE(rig.record(epoch).status == PoseRecordStatus::Recorded);
    CHECK(rig.spill->pendingBytes() > 0);
    const auto writes = rig.spill->stats().writes.load();
    const auto oldAnchor = rig.history->newest().get()->header.key;
    rig.history->clear();
    rig.pumpAll();
    CHECK(rig.spill->stats().writes.load() == writes);
    CHECK(rig.spill->pendingBytes() == 0);
    CHECK_FALSE(rig.history->before(oldAnchor, 0));
    for (unsigned epoch = 30 * kFps; epoch <= 50 * kFps; ++epoch) {
        REQUIRE(rig.record(epoch).status == PoseRecordStatus::Recorded);
        rig.pumpAll();
    }
    CHECK(rig.recallWalk() == rig.history->count());
}

TEST_CASE("a read failure marks the group lost and playback stops before it") {
    Rig rig(700, 512, 10ull * 1'000'000'000ull);
    for (unsigned epoch = 1; epoch <= 30 * kFps; ++epoch) {
        REQUIRE(rig.record(epoch).status == PoseRecordStatus::Recorded);
        rig.pumpAll();
    }
    const auto anchor = rig.history->newest().get()->header.key;
    const auto last = rig.history->count() - 1;
    rig.file.failReads = true;
    rig.spill->setPlayback(true, anchor.generation, anchor.serial);
    rig.pumpAll();
    const auto probe = rig.history->spillAvailableThrough(anchor, 0, last);
    CHECK(probe.blocked == SpillAvailability::Lost);
    CHECK(probe.through < last);
    CHECK(rig.spill->stats().lost.load() > 0);
}

}
