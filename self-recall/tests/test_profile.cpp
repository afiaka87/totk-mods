#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "PoseTestSupport.hpp"
#include "RecallBase.hpp"
#include "RecallPoseData.hpp"

namespace self_recall_tests::test_recall_profile {
using namespace self_recall::pure;

TEST_CASE("storage profile constants and allocation budgets remain exact") {
#if SELF_RECALL_STORAGE_PROFILE == 7
    CHECK(std::string_view{kStorageProfileName} == "switch-compressed");
    CHECK(kReducedHistory);
    CHECK(kHistoryCapacity == 902);
    CHECK(kHistorySeconds == 30);
    CHECK(kPosePayloadArenaBytes == 10 * kMiB);
    CHECK(kAppearancePoolBytes == 5 * kMiB / 4);
    CHECK(kArchiveHeapBytes == 2 * kMiB);
#else
    CHECK(std::string_view{kStorageProfileName} == "emulator-compressed");
    CHECK_FALSE(kReducedHistory);
    CHECK(kHistoryCapacity == 3840);
    CHECK(kHistorySeconds == 64);
    CHECK(kPosePayloadArenaBytes == 72 * kMiB);
    CHECK(kAppearancePoolBytes == 16 * kMiB);
    CHECK(kArchiveHeapBytes == 16 * kMiB);
#endif
    CHECK(kPosePayloadArenaBytes % sizeof(PosePayloadBlock) == 0);
    CHECK(kPosePayloadBlockCount == kPosePayloadArenaBytes / sizeof(PosePayloadBlock));
    CHECK(kAppearancePoolBytes % kAppearanceBlockBytes == 0);
    CHECK(kAppearanceBlockCount == kAppearancePoolBytes / kAppearanceBlockBytes);
    CHECK(kArchiveHeapBytes % 4096 == 0);
    CHECK(sizeof(PoseHistorySlot) * kHistoryCapacity + sizeof(PoseHistory) +
          kPosePayloadArenaBytes <= kPoseHistoryByteLimit);
}

TEST_CASE("game time reaches the configured window at 30 and 60 Hz") {
    for (const unsigned fps : {30u, 60u}) {
        GameTime clock;
        GameTimeSnapshot sample;
        for (unsigned frame = 0; frame < kHistorySeconds * fps; ++frame)
            sample = clock.update(fps == 60 ? 0.5f : 1.0f, false);
        CHECK(sample.status == GameTimeStatus::Running);
        CHECK(sample.elapsedNanoseconds == kRecallWindowNanoseconds);
    }

    GameTime mixed;
    for (unsigned i = 0; i < 30; ++i) mixed.update(1.0f, false);
    GameTimeSnapshot end;
    for (unsigned i = 0; i < 60; ++i) end = mixed.update(0.5f, false);
    CHECK(end.elapsedNanoseconds == 2'000'000'000);
}

TEST_CASE("packed history retains its configured time or frame capacity") {
    for (const unsigned fps : {30u, 60u}) {
        PoseTestStorage storage{kHistoryCapacity, kPosePayloadBlockCount};
        PoseTestInput input{17, 488, kPoseMaterialLimit};
        input.value.header.route.flags = SampleAdmissible;
        for (unsigned epoch = 1; epoch <= fps * 70; ++epoch) {
            input.at(epoch, fps);
            for (auto& bone : input.bones) {
                for (unsigned word = 0; word < 16; ++word) {
                    bone.words[word] = word < 8
                        ? (word % 2 ? 0x3f800000u : 0u)
                        : 0x3f000000u + epoch * 16 + word;
                }
            }
            REQUIRE(storage.history.record(input.value).status == PoseRecordStatus::Recorded);
        }

        const auto expected = std::min<unsigned>(kHistoryCapacity, fps * kHistorySeconds + 1);
        REQUIRE(storage.history.count() == expected);
        auto newest = storage.history.newest();
        auto oldest = storage.history.newest(expected - 1);
        REQUIRE(newest);
        REQUIRE(oldest);
        const auto capacityDuration = std::uint64_t{kHistoryCapacity - 1} * 1'000'000'000 / fps;
        const auto frameDuration = 1'000'000'000 / fps;
        CHECK(newest.get()->header.elapsedNanoseconds - oldest.get()->header.elapsedNanoseconds >=
              std::min<std::uint64_t>(kRecallWindowNanoseconds, capacityDuration) - frameDuration - 1);
        CHECK(oldest.get()->bones[487].words[15] ==
              0x3f000000u + (fps * 70 - expected + 1) * 16 + 15);
        storage.history.clear();
        CHECK(oldest.get()->header.frameEpoch != 0);
    }
}

#if SELF_RECALL_STORAGE_PROFILE == 8
TEST_CASE("emulator arena retains a full window of incompressible maximum poses") {
    PoseTestStorage storage{kHistoryCapacity, kPosePayloadBlockCount};
    PoseTestInput input{32, 512, kPoseMaterialLimit};
    input.value.header.route.flags = SampleAdmissible;
    std::uint32_t random = 123456789;
    for (unsigned epoch = 1; epoch <= 2100; ++epoch) {
        input.at(epoch, 30);
        for (auto& bone : input.bones) {
            for (auto& word : bone.words) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                word = random;
            }
        }
        REQUIRE(storage.history.record(input.value).status == PoseRecordStatus::Recorded);
    }

    REQUIRE(storage.history.count() == kHistorySeconds * 30 + 1);
    auto newest = storage.history.newest();
    auto oldest = storage.history.newest(kHistorySeconds * 30);
    REQUIRE(newest);
    REQUIRE(oldest);
    CHECK(newest.get()->header.elapsedNanoseconds - oldest.get()->header.elapsedNanoseconds ==
          kRecallWindowNanoseconds);
    CHECK(std::memcmp(newest.get()->bones, input.bones.data(), sizeof(newest.get()->bones)) == 0);
}
#endif

}
