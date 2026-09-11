#include <limits>
#include <memory>
#include <initializer_list>

#include "RecallGameTime.hpp"
#include "RecallPoseHistory.hpp"
#include "doctest.h"

using namespace self_recall::pure;

TEST_CASE("game time preserves duration at native 30 and 60 Hz") {
    for (const unsigned fps : {30u, 60u}) {
        GameTime clock;
        GameTimeSnapshot sample;
        for (unsigned frame = 0; frame < 64 * fps; ++frame)
            sample = clock.update(fps == 60 ? 0.5f : 1.0f, false);
        CHECK(sample.status == GameTimeStatus::Running);
        CHECK(sample.elapsedNanoseconds == kRecallWindowNanoseconds);
    }
    GameTime mixed;
    for (unsigned i = 0; i < 30; ++i) mixed.update(1.0f, false);
    GameTimeSnapshot end;
    for (unsigned i = 0; i < 60; ++i) end = mixed.update(0.5f, false);
    CHECK(end.elapsedNanoseconds == 2000000000ull);
}

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
        auto slots = std::make_unique<PoseHistorySlot[]>(256);
        std::unique_ptr<PosePayloadBlock[]> payload = std::make_unique<PosePayloadBlock[]>(256 * 36);
        PoseHistory history(slots.get(), 256, {payload.get(), 256 * 36});
        RecordedModelPose model{};
        model.identity = {1, 2, 3, 0, 1, 0, 0};
        RecordedBoneMatrix bone{};
        PoseFrameInput input{};
        input.models = &model;
        input.bones = &bone;
        input.header.modelCount = input.header.boneCount = 1;
        input.header.worldGeneration = input.header.modelGeneration = 1;
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
