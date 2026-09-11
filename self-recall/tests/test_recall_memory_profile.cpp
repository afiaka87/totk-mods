#include <doctest.h>
#include "RecallMemoryProfile.hpp"
#include "RecallAppearanceHistory.hpp"
using namespace self_recall::pure;

TEST_CASE("memory history accounting handles wrap trim and generation without changing peak") {
    HistoryMemoryUsage<3> m;
    m.record(0, 10, 1, 1); m.record(1, 20, 1, 2); m.record(2, 30, 1, 3);
    CHECK(m.bytes == 60); CHECK(m.peakBytes == 60); CHECK(m.peakFrames == 3);
    m.record(0, 40, 1, 3);
    CHECK(m.bytes == 90); CHECK(m.peakBytes == 90);
    m.record(1, 50, 1, 2);
    CHECK(m.bytes == 90);
    m.record(0, 7, 2, 1);
    CHECK(m.bytes == 7); CHECK(m.peakBytes == 90);
    m.record(1, 9, 2, 2);
    CHECK(m.bytes == 16);
    m.record(9, 999, 2, 2);
    CHECK(m.bytes == 16);
}
TEST_CASE("appearance memory counts shared states once and retains transient allocation peaks") {
    AppearanceBlobs<4, 4, 16> blobs;
    blobs.initialize();
    const std::array<std::byte, 17> a{};
    const std::array<std::byte, 16> b{};
    const auto first = blobs.create(a);
    REQUIRE(first); REQUIRE(blobs.retain(first));
    CHECK(blobs.memoryUsage().liveBytes == 17);
    CHECK(blobs.memoryUsage().liveAllocated == 32);
    CHECK(blobs.memoryUsage().liveStates == 1);
    CHECK(blobs.memoryUsage().references == 2);
    const auto second = blobs.create(b);
    REQUIRE(second);
    CHECK(blobs.memoryUsage().peakBytes == 33);
    CHECK(blobs.memoryUsage().peakAllocated == 48);
    CHECK(blobs.create(a) == 0);
    CHECK(blobs.memoryUsage().failures == 1);
    blobs.release(first);
    CHECK(blobs.memoryUsage().liveBytes == 33);
    blobs.release(first);
    CHECK(blobs.memoryUsage().liveBytes == 16);
    CHECK(blobs.memoryUsage().liveAllocated == 16);
    blobs.release(second);
    CHECK(blobs.memoryUsage().liveBytes == 0);
    CHECK(blobs.memoryUsage().liveStates == 0);
    CHECK(blobs.memoryUsage().references == 0);
    CHECK(blobs.memoryUsage().peakReferences == 3);
    CHECK(blobs.memoryUsage().peakStates == 2);
    CHECK(blobs.memoryUsage().peakAllocated == 48);
    blobs.initialize();
    CHECK(blobs.memoryUsage().creates == 0);
    CHECK(blobs.memoryUsage().peakBytes == 0);
}

TEST_CASE("history memory accounting matches a sliding reference across repeated wraps") {
    HistoryMemoryUsage<7> m;
    std::array<unsigned, 7> sizes{};
    unsigned retained = 0;
    std::uint64_t peak = 0;
    for (unsigned serial = 0; serial < 500; ++serial) {
        const unsigned slot = serial % 7;
        sizes[slot] = (serial * 37) % 251 + 1;
        retained = std::min(retained + 1, 7u);
        if (serial % 11 == 0) retained = std::min(retained, 2u);
        std::uint64_t reference = 0;
        for (unsigned back = 0; back < retained; ++back) reference += sizes[(slot + 7 - back) % 7];
        peak = std::max(peak, reference);
        m.record(slot, sizes[slot], 1, retained);
        CHECK(m.bytes == reference);
        CHECK(m.peakBytes == peak);
        CHECK(m.gaps == 0);
    }
}
