#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include <doctest.h>

#include "RecallGraphicsEngine.hpp"
#include "RecallRender.hpp"
#include "RecallVisual.hpp"

namespace self_recall_tests::test_recall_gpu_buffer {
using namespace self_recall::palette;

namespace {
template<class T> void put(void* pointer, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(pointer) + offset, &value, sizeof(value));
}
struct FakeGpu {
    inline static FakeGpu* current = nullptr;
    alignas(4096) std::array<std::byte, 0x30000> storage{};
    std::array<std::byte, 8> device{};
    std::vector<std::string> calls;
    struct Range { std::ptrdiff_t offset; std::size_t size; };
    std::vector<Range> flushes;
    bool poolInitializationFails = false, initializationFails = false;
    bool noMapping = false, foreignMapping = false;
    std::uint64_t gpu = 0x10000000;
    std::uint32_t poolFlags = 0x24, reportedPoolFlags = 0;
    void* pool = nullptr;
    std::size_t poolBytes = 0, bufferBytes = 0;

    FakeGpu() { REQUIRE(current == nullptr); current = this; storage.fill(std::byte{0xCC}); }
    ~FakeGpu() { current = nullptr; }
    GpuBufferApi api() {
        return {device.data(), poolFlags,
            [](void*) { current->calls.push_back("pool-defaults"); },
            [](void*, void* device) { CHECK(device == current->device.data()); },
            [](void*, void* storage, std::size_t bytes) {
                CHECK(storage == current->storage.data());
                CHECK(bytes <= current->storage.size());
                CHECK((bytes & 4095) == 0);
                current->poolBytes = bytes;
            },
            [](void*, int flags) { CHECK(flags == current->poolFlags); },
            [](void* pool, const void*) -> std::uint8_t {
                current->calls.push_back("pool-initialize");
                current->pool = pool;
                return current->poolInitializationFails ? 0 : 1;
            },
            [](void* pool) {
                CHECK(pool == current->pool);
                current->calls.push_back("pool-finalize");
            },
            [](const void* pool) -> std::uint32_t {
                CHECK(pool == current->pool);
                return current->reportedPoolFlags ? current->reportedPoolFlags : current->poolFlags;
            },
            [](void*) { current->calls.push_back("defaults"); },
            [](void*, void* device) { CHECK(device == current->device.data()); },
            [](void*, void* pool, std::ptrdiff_t offset, std::size_t bytes) {
                CHECK(pool == current->pool);
                CHECK(offset == 0);
                CHECK(bytes <= current->poolBytes);
                current->bufferBytes = bytes;
            },
            [](void*, const void*) -> std::uint8_t {
                current->calls.push_back("initialize");
                return current->initializationFails ? 0 : 1;
            },
            [](void*) { current->calls.push_back("finalize"); },
            [](const void*) -> void* {
                auto& f = *current;
                f.calls.push_back("map");
                if (f.noMapping) return nullptr;
                return f.storage.data() + (f.foreignMapping ? 256 : 0);
            },
            [](const void*) -> std::uint64_t {
                current->calls.push_back("address"); return current->gpu;
            },
            [](const void*, std::ptrdiff_t offset, std::size_t bytes) {
                current->calls.push_back("flush");
                current->flushes.push_back({offset, bytes});
            }};
    }
    bool called(const char* name) const { return std::find(calls.begin(), calls.end(), name) != calls.end(); }
    void checkReleased(bool initialized, bool poolInitialized = true) const {
        REQUIRE_FALSE(calls.empty());
        if (poolInitialized) CHECK(calls.back() == "pool-finalize");
        CHECK(std::count(calls.begin(), calls.end(), "pool-finalize") == (poolInitialized ? 1 : 0));
        CHECK(std::count(calls.begin(), calls.end(), "finalize") == (initialized ? 1 : 0));
        if (initialized) CHECK(calls[calls.size() - 2] == "finalize");
    }
};
static_assert(!std::is_copy_constructible_v<NativeGpuBuffer>);
static_assert(!std::is_move_constructible_v<NativeGpuBuffer>);
}

TEST_CASE("palette pool keeps the native uniform cache policy for attribute two") {
    CHECK(uniformPoolFlags(0, 0) == 0x24);
    CHECK(uniformPoolFlags(2, 0) == 0x22);
    CHECK(uniformPoolFlags(0, 2) == 0x14);
    CHECK(uniformPoolFlags(2, 2) == 0x12);
    CHECK(uniformPoolFlags(0xFFFFFFFD, 0xFFFFFFFD) == 0x24);
}

TEST_CASE("maximum palette capacity uses exactly the reserved module backing") {
    FakeGpu f;
    NativeGpuBuffer buffer;
    REQUIRE(buffer.create(f.api(), f.storage, UINT16_MAX, 3) == GpuBufferStatus::Ready);
    CHECK(f.poolBytes == kPaletteBackingBytes);
    CHECK(f.bufferBytes == kPaletteBackingBytes);
    CHECK(buffer.layout().total == kPaletteBackingBytes);
}

TEST_CASE("palette GPU patches preserve native bytes and clear an older larger layout") {
    FakeGpu f;
    NativeGpuBuffer buffer;
    REQUIRE(buffer.create(f.api(), f.storage, 1024, 3) == GpuBufferStatus::Ready);
    std::array<std::byte, 768> old{};
    old.fill(std::byte{0xA5});
    REQUIRE(buffer.uploadPatched(1, old, {}) == GpuBufferStatus::Ready);
    std::array<std::byte, 580> current{};
    for (std::size_t i = 0; i < current.size(); ++i) current[i] = std::byte(i % 253);
    const auto untouched = current;
    const std::array<GpuWordPatch, 2> patches{{{256, std::bit_cast<std::uint32_t>(1.0f)},
                                              {572, std::bit_cast<std::uint32_t>(1.0f)}}};
    REQUIRE(buffer.uploadPatched(1, current, patches) == GpuBufferStatus::Ready);
    CHECK(current == untouched);
    auto expected = current;
    for (auto patch : patches) put(expected.data(), patch.offset, patch.value);
    CHECK(std::equal(expected.begin(), expected.end(), f.storage.begin() + 1024));
    CHECK(std::all_of(f.storage.begin(), f.storage.begin() + 1024, [](auto b) { return b == std::byte{}; }));
    CHECK(std::all_of(f.storage.begin() + 1024 + 580, f.storage.begin() + 3072,
                      [](auto b) { return b == std::byte{}; }));
    CHECK(f.flushes.back().offset == 1024);
    CHECK(f.flushes.back().size == 1024);
    REQUIRE(buffer.addressForSubmission(1) != 0);
    CHECK(buffer.uploadPatched(1, current, patches) == GpuBufferStatus::SlotInFlight);
    REQUIRE(buffer.retireAfterGpuFence(1));
}

TEST_CASE("invalid palette patches cannot partly overwrite a published layout") {
    FakeGpu f;
    NativeGpuBuffer buffer;
    REQUIRE(buffer.create(f.api(), f.storage, 580, 3) == GpuBufferStatus::Ready);
    std::array<std::byte, 580> bytes{};
    bytes.fill(std::byte{0xA7});
    REQUIRE(buffer.upload(0, bytes) == GpuBufferStatus::Ready);
    const auto before = f.storage;
    auto patches = std::array<GpuWordPatch, 2>{{{256, 0}, {580, 0}}};
    SUBCASE("second patch is beyond source") {}
    SUBCASE("second patch is unaligned") { patches[1].offset = 257; }
    SUBCASE("patches overlap") { patches[1].offset = 256; }
    SUBCASE("large offset") { patches[1].offset = UINT32_MAX - 3; }
    CHECK(buffer.uploadPatched(0, bytes, patches) == GpuBufferStatus::InvalidPatch);
    CHECK(f.storage == before);
    CHECK(buffer.addressForSubmission(0) != 0);
    REQUIRE(buffer.retireAfterGpuFence(0));
}

TEST_CASE("GPU palette allocation is bounded before any native call") {
    CHECK(gpuBufferLayout(0, 3).total == 0);
    CHECK(gpuBufferLayout(UINT16_MAX + 1u, 3).total == 0);
    CHECK(gpuBufferLayout(580, 0).total == 0);
    CHECK(gpuBufferLayout(580, 4).total == 0);
    CHECK(gpuBufferLayout(UINT32_MAX, UINT32_MAX).total == 0);
    CHECK(gpuBufferLayout(580, 3).stride == 768);
    CHECK(gpuBufferLayout(580, 3).total == 2304);
    CHECK(gpuBufferLayout(UINT16_MAX, 3).total == 196608);
    FakeGpu f;
    NativeGpuBuffer buffer;
    CHECK(buffer.create(f.api(), f.storage, 0, 3) == GpuBufferStatus::InvalidLayout);
    CHECK(buffer.create({}, f.storage, 580, 3) == GpuBufferStatus::InvalidApi);
    CHECK(buffer.create(f.api(), {}, 580, 3) == GpuBufferStatus::InvalidBacking);
    CHECK(buffer.create(f.api(), std::span(f.storage).subspan(1), 580, 3) == GpuBufferStatus::InvalidBacking);
    CHECK(buffer.create(f.api(), std::span(f.storage).first(4095), 580, 3) == GpuBufferStatus::InvalidBacking);
    for (auto flags : {0u, 1u, 6u, 0xCu, 0x3Cu, 0x80000024u}) {
        auto invalid = f.api(); invalid.memoryPoolFlags = flags;
        CHECK(buffer.create(invalid, f.storage, 580, 3) == GpuBufferStatus::InvalidApi);
    }
    auto missing = f.api(); missing.flush = nullptr;
    CHECK(buffer.create(missing, f.storage, 580, 3) == GpuBufferStatus::InvalidApi);
    CHECK(f.calls.empty());
    CHECK(buffer.addressForSubmission(0) == 0);
    CHECK(buffer.upload(0, {}) == GpuBufferStatus::NotReady);
    CHECK_FALSE(buffer.retireAfterGpuFence(0));
    CHECK(f.calls.empty());
}

TEST_CASE("GPU palette failure releases only initialized graphics resources in order") {
    FakeGpu f;
    NativeGpuBuffer buffer;
    auto expected = GpuBufferStatus::PoolInitFailed;
    bool graphicsInitialized = false, poolInitialized = true;
    SUBCASE("pool initialization fails") { f.poolInitializationFails = true; poolInitialized = false; }
    SUBCASE("pool disallows CPU access") { f.reportedPoolFlags = 0x21; expected = GpuBufferStatus::InvalidMapping; }
    SUBCASE("pool has contradictory CPU flags") { f.reportedPoolFlags = 0x26; expected = GpuBufferStatus::InvalidMapping; }
    SUBCASE("pool changes GPU policy") { f.reportedPoolFlags = 0x14; expected = GpuBufferStatus::InvalidMapping; }
    SUBCASE("graphics initialization fails") { f.initializationFails = true; expected = GpuBufferStatus::GraphicsInitFailed; }
    SUBCASE("map is null") { f.noMapping = true; expected = GpuBufferStatus::InvalidMapping; graphicsInitialized = true; }
    SUBCASE("map is another allocation") { f.foreignMapping = true; expected = GpuBufferStatus::InvalidMapping; graphicsInitialized = true; }
    SUBCASE("GPU address is null") { f.gpu = 0; expected = GpuBufferStatus::InvalidAddress; graphicsInitialized = true; }
    SUBCASE("GPU address is unaligned") { f.gpu = 1; expected = GpuBufferStatus::InvalidAddress; graphicsInitialized = true; }
    SUBCASE("GPU range overflows") { f.gpu = UINT64_MAX - 255; expected = GpuBufferStatus::InvalidAddress; graphicsInitialized = true; }
    REQUIRE(buffer.create(f.api(), f.storage, 580, 3) == expected);
    CHECK_FALSE(buffer.ready());
    CHECK(buffer.layout().total == 0);
    for (unsigned i = 0; i < 4; ++i) CHECK(buffer.addressForSubmission(i) == 0);
    CHECK_FALSE(f.called("flush"));
    f.checkReleased(graphicsInitialized, poolInitialized);
}

TEST_CASE("GPU palette uploads preserve separate slots and zero binding padding") {
    FakeGpu f;
    NativeGpuBuffer buffer;
    REQUIRE(buffer.create(f.api(), f.storage, 580, 3) == GpuBufferStatus::Ready);
    REQUIRE(buffer.ready());
    CHECK(buffer.create(f.api(), f.storage, 580, 3) == GpuBufferStatus::AlreadyCreated);
    REQUIRE(f.flushes.size() == 1);
    CHECK(f.flushes[0].offset == 0);
    CHECK(f.flushes[0].size == 2304);
    for (unsigned slot = 0; slot < 3; ++slot) CHECK(buffer.addressForSubmission(slot) == 0);
    std::array<std::byte, 580> data{};
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::byte>((i * 19u) & 255u);
    REQUIRE(buffer.upload(1, data) == GpuBufferStatus::Ready);
    for (std::size_t i = 0; i < 2304; ++i) {
        const auto wanted = i >= 768 && i < 768 + data.size() ? data[i - 768] : std::byte{};
        CHECK(f.storage[i] == wanted);
    }
    CHECK(f.storage[2304] == std::byte{0xCC});
    REQUIRE(f.flushes.size() == 2);
    CHECK(f.flushes[1].offset == 768);
    CHECK(f.flushes[1].size == 768);
    CHECK(buffer.addressForSubmission(0) == 0);
    CHECK(buffer.addressForSubmission(1) == f.gpu + 768);
    CHECK(buffer.addressForSubmission(2) == 0);
    CHECK(buffer.addressForSubmission(3) == 0);
    CHECK(buffer.retireAfterGpuFence(1));
}

TEST_CASE("GPU palette submission prevents overwrite until explicit retirement") {
    FakeGpu f;
    NativeGpuBuffer buffer;
    REQUIRE(buffer.create(f.api(), f.storage, 16, 3) == GpuBufferStatus::Ready);
    std::array<std::byte, 16> original{}, replacement{};
    original.fill(std::byte{0x31}); replacement.fill(std::byte{0x72});
    REQUIRE(buffer.upload(0, original) == GpuBufferStatus::Ready);
    REQUIRE(buffer.addressForSubmission(0) == f.gpu);
    CHECK(buffer.addressForSubmission(0) == f.gpu);
    CHECK(buffer.upload(0, replacement) == GpuBufferStatus::SlotInFlight);
    CHECK_FALSE(f.called("finalize"));
    CHECK_FALSE(f.called("pool-finalize"));
    REQUIRE(buffer.upload(1, replacement) == GpuBufferStatus::Ready);
    REQUIRE(buffer.addressForSubmission(1) == f.gpu + 256);
    CHECK(buffer.retireAfterGpuFence(1));
    CHECK(buffer.upload(0, replacement) == GpuBufferStatus::SlotInFlight);
    CHECK(buffer.retireAfterGpuFence(0));
    CHECK(buffer.addressForSubmission(0) == 0);
    REQUIRE(buffer.upload(0, replacement) == GpuBufferStatus::Ready);
    CHECK(std::equal(replacement.begin(), replacement.end(), f.storage.begin()));
}

TEST_CASE("GPU palette rejects invalid writes without corrupting a valid uploaded slot") {
    FakeGpu f;
    NativeGpuBuffer buffer;
    REQUIRE(buffer.create(f.api(), f.storage, 16, 2) == GpuBufferStatus::Ready);
    std::array<std::byte, 16> data{}; data.fill(std::byte{0x53});
    REQUIRE(buffer.upload(0, data) == GpuBufferStatus::Ready);
    CHECK(buffer.upload(2, data) == GpuBufferStatus::InvalidSlot);
    CHECK(buffer.upload(UINT32_MAX, data) == GpuBufferStatus::InvalidSlot);
    CHECK(buffer.upload(0, {}) == GpuBufferStatus::InvalidBytes);
    CHECK(buffer.upload(0, std::span(data).first(15)) == GpuBufferStatus::InvalidBytes);
    CHECK(buffer.upload(0, {f.storage.data(), 16}) == GpuBufferStatus::OverlappingBytes);
    CHECK(buffer.upload(0, {f.storage.data() + 256, 16}) == GpuBufferStatus::OverlappingBytes);
    CHECK(f.flushes.size() == 2);
    CHECK(std::equal(data.begin(), data.end(), f.storage.begin()));
    CHECK(buffer.addressForSubmission(0) == f.gpu);
    CHECK(buffer.retireAfterGpuFence(0));
    CHECK_FALSE(buffer.retireAfterGpuFence(2));
}

TEST_CASE("GPU palette allocation can retry after a failed initialization") {
    FakeGpu f;
    NativeGpuBuffer buffer;
    f.initializationFails = true;
    REQUIRE(buffer.create(f.api(), f.storage, 64, 1) == GpuBufferStatus::GraphicsInitFailed);
    f.checkReleased(false);
    f.calls.clear();
    f.initializationFails = false;
    REQUIRE(buffer.create(f.api(), f.storage, 64, 1) == GpuBufferStatus::Ready);
}

TEST_CASE("GPU palette respects native cached and uncached pool access") {
    FakeGpu f;
    NativeGpuBuffer buffer;
    f.poolFlags = 0x22;
    REQUIRE(buffer.create(f.api(), f.storage, 16, 1) == GpuBufferStatus::Ready);
    std::array<std::byte, 16> data{}; data.fill(std::byte{0x29});
    REQUIRE(buffer.upload(0, data) == GpuBufferStatus::Ready);
    CHECK(std::equal(data.begin(), data.end(), f.storage.begin()));
    CHECK(f.flushes.empty());
    CHECK(buffer.addressForSubmission(0) == f.gpu);
    CHECK(buffer.retireAfterGpuFence(0));
}
}

namespace self_recall_tests::test_recall_gpu_lifetime {
using self_recall::pure::RecallGpuLifetime;

namespace {
constexpr std::uintptr_t poolId = 10, listObjects = 0x1000, listCb = 20, rootCb = 30;
constexpr std::uintptr_t queue = 40, sync = 50;
constexpr std::uint32_t objectStride = 120;
struct Lifetime {
    RecallGpuLifetime ledger;
    Lifetime() { REQUIRE(ledger.configurePool(poolId, listObjects, 4, objectStride)); }
    void record(unsigned slot) {
        REQUIRE(ledger.beginList(listCb, poolId));
        REQUIRE(ledger.bindSlot(listCb, slot));
        REQUIRE(ledger.endList(listCb));
    }
    void frame(std::uint64_t handle) {
        REQUIRE(ledger.beginFrame(rootCb));
        REQUIRE(ledger.callList(rootCb, listObjects));
        REQUIRE(ledger.sealFrame(rootCb, handle));
    }
};
}

TEST_CASE("recording completion and native pool rotation cannot retire pending GPU uses") {
    Lifetime f;
    CHECK(f.ledger.canWrite(0));
    f.record(0);
    CHECK_FALSE(f.ledger.canWrite(0));
    f.frame(101);
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK_FALSE(f.ledger.canWrite(0));
    const auto submitted = f.ledger.submittedAndFenced(101, queue, sync);
    REQUIRE(submitted != 0);
    CHECK_FALSE(f.ledger.canWrite(0));
    const auto waited = f.ledger.captureWait(queue, sync);
    CHECK_FALSE(f.ledger.completeWait(waited, 2));
    CHECK_FALSE(f.ledger.completeWait(waited, 3));
    CHECK_FALSE(f.ledger.canWrite(0));
    CHECK(f.ledger.completeWait(waited, 1));
    CHECK(f.ledger.canWrite(0));
    CHECK(f.ledger.canWrite(1));
}

TEST_CASE("a completed old fence cannot retire a later prepared or submitted frame") {
    Lifetime f;
    f.record(0);
    f.frame(101);
    const auto first = f.ledger.submittedAndFenced(101, queue, sync);
    REQUIRE(first != 0);
    const auto oldWait = f.ledger.captureWait(queue, sync);
    REQUIRE(f.ledger.clearPool(poolId));
    f.record(1);
    f.frame(102);
    CHECK(f.ledger.completeWait(oldWait, 0));
    CHECK(f.ledger.canWrite(0));
    CHECK_FALSE(f.ledger.canWrite(1));
    const auto second = f.ledger.submittedAndFenced(102, queue, sync);
    REQUIRE(second > first);
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK(f.ledger.completeWait(oldWait, 1));
    CHECK_FALSE(f.ledger.canWrite(1));
    CHECK(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 0));
    CHECK(f.ledger.canWrite(1));
}

TEST_CASE("display list storage pins slots until discard even when never submitted") {
    Lifetime f;
    f.record(2);
    CHECK_FALSE(f.ledger.canWrite(2));
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK(f.ledger.canWrite(2));
    f.record(1);
    f.frame(101);
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync) != 0);
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK_FALSE(f.ledger.canWrite(1));
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK(f.ledger.canWrite(1));
}

TEST_CASE("direct and nested list commands transfer private slot use to their root frame") {
    Lifetime f;
    constexpr std::uintptr_t otherPool = 11, otherObjects = 0x2000, otherCb = 21;
    REQUIRE(f.ledger.configurePool(otherPool, otherObjects, 2, objectStride));
    f.record(0);
    REQUIRE(f.ledger.beginList(otherCb, otherPool));
    REQUIRE(f.ledger.callList(otherCb, listObjects));
    REQUIRE(f.ledger.endList(otherCb));
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK_FALSE(f.ledger.canWrite(0));
    REQUIRE(f.ledger.beginFrame(rootCb));
    REQUIRE(f.ledger.bindSlot(rootCb, 1));
    REQUIRE(f.ledger.callList(rootCb, otherObjects));
    REQUIRE(f.ledger.sealFrame(rootCb, 101));
    REQUIRE(f.ledger.clearPool(otherPool));
    CHECK_FALSE(f.ledger.canWrite(0));
    CHECK_FALSE(f.ledger.canWrite(1));
    CHECK(f.ledger.canWrite(2));
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync) != 0);
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK(f.ledger.canWrite(0));
    CHECK(f.ledger.canWrite(1));
}

TEST_CASE("repeated root submission requires the later GPU fence before buffer reuse") {
    Lifetime f;
    f.record(0);
    f.frame(101);
    REQUIRE(f.ledger.clearPool(poolId));
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync) != 0);
    const auto priorWait = f.ledger.captureWait(queue, sync);
    REQUIRE(f.ledger.completeWait(priorWait, 1));
    CHECK(f.ledger.canWrite(0));
    REQUIRE(f.ledger.beginSubmission(101));
    CHECK_FALSE(f.ledger.canWrite(0));
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync) > priorWait);
    REQUIRE(f.ledger.completeWait(priorWait, 1));
    CHECK_FALSE(f.ledger.canWrite(0));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK(f.ledger.canWrite(0));
}

TEST_CASE("foreign waits and fabricated future tickets cannot release a private buffer") {
    Lifetime f;
    f.record(0);
    f.frame(101);
    REQUIRE(f.ledger.clearPool(poolId));
    const auto ticket = f.ledger.submittedAndFenced(101, queue, sync);
    REQUIRE(ticket != 0);
    CHECK(f.ledger.captureWait(queue + 1, sync) == 0);
    CHECK(f.ledger.captureWait(queue, sync + 1) == 0);
    CHECK_FALSE(f.ledger.completeWait(0, 1));
    CHECK_FALSE(f.ledger.completeWait(ticket + 1, 1));
    CHECK_FALSE(f.ledger.canWrite(0));
}

TEST_CASE("unknown command ownership or lost recording state freezes writes") {
    Lifetime f;
    SUBCASE("unregistered command buffer") { CHECK_FALSE(f.ledger.bindSlot(900, 0)); }
    SUBCASE("reset racing active list recording") {
        REQUIRE(f.ledger.beginList(listCb, poolId));
        CHECK_FALSE(f.ledger.clearPool(poolId));
    }
    SUBCASE("unknown submitted root") { CHECK(f.ledger.submittedAndFenced(999, queue, sync) == 0); }
    SUBCASE("foreign queue replaces bound queue") {
        f.frame(101);
        REQUIRE(f.ledger.submittedAndFenced(101, queue, sync) != 0);
        CHECK(f.ledger.submittedAndFenced(101, queue + 1, sync) == 0);
    }
    SUBCASE("misaligned list inside registered storage") {
        REQUIRE(f.ledger.beginFrame(rootCb));
        CHECK_FALSE(f.ledger.callList(rootCb, listObjects + 1));
    }
    SUBCASE("duplicate active recorder") {
        REQUIRE(f.ledger.beginList(listCb, poolId));
        CHECK_FALSE(f.ledger.beginList(listCb, poolId));
    }
    REQUIRE(f.ledger.failed());
    for (unsigned slot = 0; slot < RecallGpuLifetime::kSlots; ++slot) CHECK_FALSE(f.ledger.canWrite(slot));
}

TEST_CASE("lifetime records are bounded and only completed frame records are recycled") {
    Lifetime f;
    SUBCASE("all frame records still awaiting submission") {
        for (unsigned i = 0; i < RecallGpuLifetime::kFrames; ++i) {
            REQUIRE(f.ledger.beginFrame(rootCb));
            REQUIRE(f.ledger.sealFrame(rootCb, 101 + i));
        }
        CHECK_FALSE(f.ledger.beginFrame(rootCb));
    }
    SUBCASE("completed records support sustained frame submission") {
        for (unsigned i = 0; i < 32; ++i) {
            f.record(i % 3);
            f.frame(101 + i);
            REQUIRE(f.ledger.clearPool(poolId));
            REQUIRE(f.ledger.submittedAndFenced(101 + i, queue, sync) != 0);
            REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
            CHECK(f.ledger.canWrite(i % 3));
        }
        CHECK_FALSE(f.ledger.failed());
    }
}

TEST_CASE("invalid pool ranges and overlapping list ownership are rejected") {
    Lifetime f;
    SUBCASE("range overflow") {
        CHECK_FALSE(f.ledger.configurePool(11, UINTPTR_MAX - 10, 2, objectStride));
    }
    SUBCASE("overlapping pools") {
        CHECK_FALSE(f.ledger.configurePool(11, listObjects + objectStride, 2, objectStride));
    }
    SUBCASE("resize a pool still containing private commands") {
        f.record(0);
        CHECK_FALSE(f.ledger.configurePool(poolId, 0x2000, 4, objectStride));
    }
    SUBCASE("invalid slot") {
        REQUIRE(f.ledger.beginFrame(rootCb));
        CHECK_FALSE(f.ledger.bindSlot(rootCb, RecallGpuLifetime::kSlots));
    }
    CHECK(f.ledger.failed());
}

TEST_CASE("foreign recordings may copy ordinary lists but cannot silently consume private ones") {
    Lifetime f;
    REQUIRE(f.ledger.callList(900, listObjects));
    REQUIRE(f.ledger.callList(900, 0x9000));
    CHECK_FALSE(f.ledger.failed());
    f.record(0);
    CHECK_FALSE(f.ledger.callList(900, listObjects));
    CHECK(f.ledger.failed());
    CHECK_FALSE(f.ledger.canWrite(0));
}

TEST_CASE("GPU recorder admission uses the actual ARM return PC, not decompiler expression addresses") {
    namespace site = self_recall::gpu_lifetime::site;
    struct Call { std::uint32_t pc, instruction, target; std::uintptr_t returnPc; };
    const Call calls[]{
        {0x96F38C, 0x97FAA53E, 0x818884, site::kModelListBeginReturn},
        {0x96F3DC, 0x97FAA4AE, 0x818694, site::kModelListEndReturn},
        {0x97709C, 0x94000132, 0x977564, site::kModelPoolClearReturn},
    };
    for (const auto& call : calls) {
        REQUIRE((call.instruction & 0xFC000000u) == 0x94000000u);
        const auto immediate = std::int64_t(call.instruction & 0x03FFFFFFu) -
            ((call.instruction & 0x02000000u) ? 0x04000000ll : 0ll);
        CHECK(std::int64_t(call.pc) + immediate * 4 == call.target);
        CHECK(call.returnPc == call.pc + 4);
    }
    for (const std::uintptr_t base : {std::uintptr_t{0x8052B000}, std::uintptr_t{0x7100000000}}) {
        CHECK(site::isModelListBegin(base, base + calls[0].pc + 4));
        CHECK_FALSE(site::isModelListBegin(base, base + 0x96F394));
        CHECK_FALSE(site::isModelListBegin(base, base + 0x81808C));
        CHECK_FALSE(site::isModelListBegin(base, base - 1));
    }
}

TEST_CASE("all native layer-list return PCs are admitted with the model-list lane") {
    namespace site = self_recall::gpu_lifetime::site;
    constexpr std::uint32_t instructions[]{
        0x940004EF, 0x940004AF, 0x9400037D, 0x9400032D, 0x940001FF, 0x940000C8, 0x97FFFE21
    };
    constexpr std::uintptr_t base = 0x8052B000;
    for (unsigned i = 0; i < 7; ++i) {
        const auto pc = site::kLayerListBeginCalls[i];
        const auto word = instructions[i];
        REQUIRE((word & 0xFC000000u) == 0x94000000u);
        const auto offset = std::int64_t(word & 0x03FFFFFFu) - ((word & 0x02000000u) ? 0x04000000ll : 0ll);
        REQUIRE(std::int64_t(pc) + 4 * offset == 0x818884);
        CHECK(site::isRenderListBegin(base, base + pc + 4));
        CHECK_FALSE(site::isRenderListBegin(base, base + pc));
        CHECK_FALSE(site::isRenderListBegin(base, base + pc + 8));
    }
    CHECK(site::isRenderListBegin(base, base + 0x96F390));
    CHECK_FALSE(site::isRenderListBegin(base, base + 0x96F394));
    CHECK_FALSE(site::isRenderListBegin(base, base - 1));
}

TEST_CASE("native model to layer to retained display to root survives source pool reset") {
    Lifetime f;
    constexpr std::uintptr_t layerCb = 23, display = 12, copies = 0x4000;
    REQUIRE(f.ledger.configurePool(display, copies, 200, objectStride));
    for (unsigned frame = 1; frame <= 180; ++frame) {
        const unsigned slot = frame % 3;
        REQUIRE(f.ledger.canWrite(slot));
        f.record(slot);
        REQUIRE(f.ledger.beginList(layerCb, poolId));
        REQUIRE(f.ledger.callList(layerCb, listObjects));
        REQUIRE(f.ledger.endList(layerCb));
        REQUIRE(f.ledger.clearPool(display));
        REQUIRE(f.ledger.copyListObject(listObjects + objectStride, copies));
        REQUIRE(f.ledger.beginFrame(rootCb));
        REQUIRE(f.ledger.callList(rootCb, copies));
        REQUIRE(f.ledger.sealFrame(rootCb, 100 + frame));
        REQUIRE(f.ledger.clearPool(poolId));
        CHECK_FALSE(f.ledger.canWrite(slot));
        REQUIRE(f.ledger.beginSubmission(100 + frame));
        REQUIRE(f.ledger.submittedAndFenced(100 + frame, queue, sync));
        REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
        CHECK_FALSE(f.ledger.canWrite(slot));
        REQUIRE(f.ledger.clearPool(display));
        CHECK(f.ledger.canWrite(slot));
    }
    CHECK_FALSE(f.ledger.failed());
}

TEST_CASE("retained display copy can be drawn again after source lists and GPU work retire") {
    Lifetime f;
    REQUIRE(f.ledger.configurePool(12, 0x4000, 2, objectStride));
    f.record(1);
    REQUIRE(f.ledger.copyListObject(listObjects, 0x4000));
    REQUIRE(f.ledger.clearPool(poolId));
    for (unsigned i = 0; i < 3; ++i) {
        REQUIRE(f.ledger.beginFrame(rootCb));
        REQUIRE(f.ledger.callList(rootCb, 0x4000));
        REQUIRE(f.ledger.sealFrame(rootCb, 101 + i));
        REQUIRE(f.ledger.submittedAndFenced(101 + i, queue, sync));
        REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
        CHECK_FALSE(f.ledger.canWrite(1));
    }
    REQUIRE(f.ledger.clearPool(12));
    CHECK(f.ledger.canWrite(1));
}

TEST_CASE("alias corruption and missing intermediate ownership still fail closed") {
    Lifetime f;
    f.record(0);
    SUBCASE("unknown layer reproduces the 05 stop") { CHECK_FALSE(f.ledger.callList(999, listObjects)); }
    SUBCASE("private copy outside registered retained storage") {
        CHECK_FALSE(f.ledger.copyListObject(listObjects, 0x9000));
    }
    SUBCASE("misaligned retained object") {
        REQUIRE(f.ledger.configurePool(12, 0x4000, 2, objectStride));
        CHECK_FALSE(f.ledger.copyListObject(listObjects, 0x4001));
    }
    CHECK(f.ledger.failed());
    CHECK_FALSE(f.ledger.canWrite(0));
}

TEST_CASE("direct list commands need a later same-queue fence and successful wait") {
    Lifetime f;
    f.record(0);
    const auto mask = f.ledger.beginDirect(listObjects);
    REQUIRE(mask == 1);
    REQUIRE(f.ledger.endDirect(mask));
    REQUIRE(f.ledger.clearPool(poolId));
    CHECK_FALSE(f.ledger.canWrite(0));
    f.frame(101);
    const auto directTicket = f.ledger.captureDirectFence();
    REQUIRE(directTicket != 0);
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync, directTicket));
    CHECK_FALSE(f.ledger.canWrite(0));
    CHECK_FALSE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 2));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK(f.ledger.canWrite(0));
}

TEST_CASE("an in-flight direct submit cannot be covered by a racing fence") {
    Lifetime f;
    f.record(0);
    const auto mask = f.ledger.beginDirect(listObjects);
    REQUIRE(mask);
    const auto tooEarly = f.ledger.captureDirectFence();
    CHECK(tooEarly == 0);
    REQUIRE(f.ledger.endDirect(mask));
    REQUIRE(f.ledger.clearPool(poolId));
    f.frame(101);
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync, tooEarly));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK_FALSE(f.ledger.canWrite(0));
    f.frame(102);
    REQUIRE(f.ledger.submittedAndFenced(102, queue, sync, f.ledger.captureDirectFence()));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK(f.ledger.canWrite(0));
}

TEST_CASE("direct submit after a fence snapshot remains pinned after that fence completes") {
    Lifetime f;
    f.record(2);
    auto mask = f.ledger.beginDirect(listObjects);
    REQUIRE(f.ledger.endDirect(mask));
    const auto oldTicket = f.ledger.captureDirectFence();
    mask = f.ledger.beginDirect(listObjects);
    REQUIRE(f.ledger.endDirect(mask));
    REQUIRE(f.ledger.clearPool(poolId));
    f.frame(101);
    REQUIRE(f.ledger.submittedAndFenced(101, queue, sync, oldTicket));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK_FALSE(f.ledger.canWrite(2));
    f.frame(102);
    REQUIRE(f.ledger.submittedAndFenced(102, queue, sync, f.ledger.captureDirectFence()));
    REQUIRE(f.ledger.completeWait(f.ledger.captureWait(queue, sync), 1));
    CHECK(f.ledger.canWrite(2));
}
}

namespace self_recall_tests::test_recall_scene_material {
using namespace self_recall::palette;

namespace {
template<class T, std::size_t N> void put(std::array<std::byte, N>& bytes, std::size_t offset, T value) {
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

struct Fixture {
    static constexpr std::uintptr_t base = 0x10000000;
    std::array<std::byte, 0x200> model{};
    std::array<std::byte, 0x80> material{};
    std::array<std::byte, 0xB0> resource{};
    std::array<std::byte, 8> assignment{};
    std::array<std::byte, 0x58> shader{};
    std::array<std::byte, 0x18 * 6> params{};
    std::array<std::byte, 580> source{};
    std::array<std::byte, 0x48 * 3> buffers{};
    std::array<std::int32_t, 6> offsets{-1, -1, -1, -1, 256, 300};
    std::array<std::byte, 640> gpu{};
    Fixture() {
        put(model, 0, base + 0x045C0570);
        put(model, 0x180, material.data());
        put(model, 0x16A, std::uint16_t{1});
        put(model, 0x15, std::uint8_t{2});
        put(material, 0, resource.data());
        put(material, 8, std::uint16_t{1});
        put(material, 0xA, std::uint8_t{3});
        put(material, 0x40, buffers.data());
        put(material, 0x48, source.data());
        put(material, 0x60, std::uint64_t{640});
        put(resource, 0x10, assignment.data());
        put(resource, 0x60, offsets.data());
        put(resource, 0xAA, std::uint16_t{640});
        put(assignment, 0, shader.data());
        put(shader, 0x20, params.data());
        put(shader, 0x4A, std::uint16_t{6});
        put(shader, 0x4C, std::uint16_t{580});
        put(params, 4 * 0x18 + 0x10, std::uint16_t{120});
        put(params, 5 * 0x18 + 0x10, std::uint16_t{124});
        put(params, 4 * 0x18 + 0x12, std::uint8_t{12});
        put(params, 5 * 0x18 + 0x12, std::uint8_t{12});
        put(source, 120, 1.0f);
        put(source, 124, 1.0f);
        for (std::size_t i = 0; i < 3; ++i) put(buffers, i * 0x48 + 8, std::uintptr_t{0x2000} + i * 0x100);
        for (std::size_t i = 0; i < gpu.size(); ++i) gpu[i] = static_cast<std::byte>(i % 253);
        put(gpu, 256, 0.7f);
        put(gpu, 300, 0.6f);
    }
    MaterialStatus describe(SceneMaterialView& view) const {
        return describeSceneMaterial(model.data(), base, 0, 4, 5, view);
    }
};
}

TEST_CASE("scene palette resolves distinct CPU and shader uniform locations and native buffer index") {
    Fixture fixture;
    SceneMaterialView view;
    REQUIRE(fixture.describe(view) == MaterialStatus::Ready);
    CHECK(view.sourceBytes == 580);
    CHECK(view.uniformBytes == 640);
    CHECK(view.character.sourceOffset == 120);
    CHECK(view.character.uniformOffset == 256);
    CHECK(view.other.sourceOffset == 124);
    CHECK(view.other.uniformOffset == 300);
    CHECK(view.bufferIndex == 2);
    CHECK(view.nativeBuffer == fixture.buffers.data() + 2 * 0x48);
    fixture.offsets[4] = 400;
    put(fixture.model, 0x15, std::uint8_t{0});
    REQUIRE(fixture.describe(view) == MaterialStatus::Ready);
    CHECK(view.character.uniformOffset == 400);
    CHECK(view.nativeBuffer == fixture.buffers.data());
}

TEST_CASE("unbound and incompatible scene shader maps are refused") {
    Fixture fixture;
    SceneMaterialView view;
    REQUIRE(fixture.describe(view) == MaterialStatus::Ready);
    fixture.offsets[4] = -1;
    CHECK(fixture.describe(view) == MaterialStatus::UnmappedParameter);
    CHECK(view.character.uniformOffset == 256);
    fixture.offsets[4] = 640;
    CHECK(fixture.describe(view) == MaterialStatus::UniformRange);
    fixture.offsets[4] = 300;
    CHECK(fixture.describe(view) == MaterialStatus::AliasedParameters);
    fixture.offsets[4] = 256;
    put(fixture.params, 4 * 0x18 + 0x12, std::uint8_t{15});
    CHECK(fixture.describe(view) == MaterialStatus::UnsupportedParameter);
    put(fixture.params, 4 * 0x18 + 0x12, std::uint8_t{12});
    put(fixture.params, 4 * 0x18, std::uintptr_t{0x1234});
    CHECK(fixture.describe(view) == MaterialStatus::UnsupportedParameter);
}

TEST_CASE("palette description rejects invalid source and native GPU buffer state") {
    Fixture fixture;
    SceneMaterialView view;
    CHECK(describeSceneMaterial(fixture.model.data(), Fixture::base, 1, 4, 5, view) == MaterialStatus::MissingMaterial);
    CHECK(describeSceneMaterial(fixture.model.data(), Fixture::base, 0, 6, 5, view) == MaterialStatus::InvalidIndex);
    CHECK(describeSceneMaterial(fixture.model.data(), Fixture::base, 0, 4, 4, view) == MaterialStatus::AliasedParameters);
    put(fixture.shader, 0x4C, std::uint16_t{124});
    CHECK(fixture.describe(view) == MaterialStatus::SourceRange);
    put(fixture.shader, 0x4C, std::uint16_t{580});
    put(fixture.source, 120, std::numeric_limits<float>::quiet_NaN());
    CHECK(fixture.describe(view) == MaterialStatus::NonfiniteSource);
    put(fixture.source, 120, 1.0f);
    put(fixture.model, 0x15, std::uint8_t{3});
    CHECK(fixture.describe(view) == MaterialStatus::BufferUnavailable);
    put(fixture.model, 0x15, std::uint8_t{2});
    put(fixture.material, 8, std::uint16_t{0});
    CHECK(fixture.describe(view) == MaterialStatus::BufferUnavailable);
    put(fixture.material, 8, std::uint16_t{1});
    put(fixture.resource, 0xAA, std::uint16_t{512});
    CHECK(fixture.describe(view) == MaterialStatus::UniformRange);
}

TEST_CASE("palette upload receipt requires final source and GPU values in the selected buffer") {
    Fixture fixture;
    SceneMaterialView view;
    REQUIRE(fixture.describe(view) == MaterialStatus::Ready);
    put(fixture.source, 120, 0.0f);
    put(fixture.source, 124, 0.0f);
    CHECK(verifyScenePaletteUpload(view, fixture.gpu, {0, 0}) == MaterialStatus::ValueMismatch);
    put(fixture.gpu, 256, 0.0f);
    put(fixture.gpu, 300, 0.0f);
    CHECK(verifyScenePaletteUpload(view, fixture.gpu, {0, 0}) == MaterialStatus::Ready);
    put(fixture.material, 0x2C, std::uint8_t{1});
    CHECK(verifyScenePaletteUpload(view, fixture.gpu, {0, 0}) == MaterialStatus::PendingUpload);
    put(fixture.material, 0x2C, std::uint8_t{0});
    put(fixture.material, 0x2D, std::uint8_t{1u << 2});
    CHECK(verifyScenePaletteUpload(view, fixture.gpu, {0, 0}) == MaterialStatus::PendingUpload);
    put(fixture.material, 0x2D, std::uint8_t{1});
    CHECK(verifyScenePaletteUpload(view, fixture.gpu, {0, 0}) == MaterialStatus::Ready);
    put(fixture.source, 120, 0.375f);
    CHECK(verifyScenePaletteUpload(view, fixture.gpu, {0, 0}) == MaterialStatus::ValueMismatch);
    put(fixture.gpu, 256, 0.375f);
    CHECK(verifyScenePaletteUpload(view, fixture.gpu, {0.375f, 0}) == MaterialStatus::Ready);
}

TEST_CASE("palette writers cannot combine different frames views or scene resources") {
    using namespace self_recall::pure;
    PaletteWrites writes;
    const PaletteWriterIdentity scene{100, 200, 300};
    const PaletteWriterIdentity replacement{100, 200, 400};
    const PalettePreparedBuffer prepared{scene, 12, 600, 1, 580};
    CHECK(prepared.matches(scene, 12, 600, 1, 580));
    CHECK_FALSE(prepared.matches(scene, 13, 600, 1, 580));
    CHECK_FALSE(prepared.matches(replacement, 12, 600, 1, 580));
    CHECK_FALSE(prepared.matches(scene, 12, 700, 1, 580));
    CHECK_FALSE(prepared.matches(scene, 12, 600, 2, 580));
    CHECK_FALSE(prepared.matches(scene, 12, 600, 1, 640));
    writes.beginFrame(12, true);
    CHECK(writes.recall(12));
    CHECK_FALSE(writes.recall(11));
    REQUIRE(writes.record(12, scene, 0, PaletteParameter::Character, 4, 0));
    CHECK_FALSE(writes.complete(12, scene));
    REQUIRE(writes.record(12, scene, 0, PaletteParameter::Other, 5, 0));
    CHECK(writes.complete(12, scene));
    CHECK_FALSE(writes.complete(12, replacement));
    REQUIRE(writes.record(12, scene, 0, PaletteParameter::Character, 4, 0.25f));
    CHECK_FALSE(writes.complete(12, scene));
    REQUIRE(writes.record(12, replacement, 0, PaletteParameter::Other, 5, 0));
    CHECK_FALSE(writes.complete(12, replacement));
    REQUIRE(writes.record(12, replacement, 0, PaletteParameter::Character, 6, 0));
    REQUIRE(writes.record(12, replacement, 0, PaletteParameter::Other, 7, 0));
    CHECK(writes.complete(12, replacement));
    CHECK(writes.index(PaletteParameter::Character) == 6);
    CHECK_FALSE(writes.record(12, replacement, 1, PaletteParameter::Character, 6, 0));
    CHECK_FALSE(writes.complete(12, replacement));
    writes.beginFrame(13, false);
    CHECK_FALSE(writes.recall(13));
    CHECK_FALSE(writes.record(12, replacement, 0, PaletteParameter::Other, 7, 0));
    CHECK_FALSE(writes.complete(13, replacement));
    REQUIRE(writes.record(13, scene, 0, PaletteParameter::Character, 4, 0.75f));
    REQUIRE(writes.record(13, scene, 0, PaletteParameter::Other, 4, 0.5f));
    CHECK_FALSE(writes.complete(13, scene));
}
}
