#include <doctest.h>
#include "RecallNativeGpuBuffer.hpp"

#include <algorithm>
#include <bit>
#include <string>
#include <type_traits>
#include <vector>

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
} // namespace

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
    CHECK(buffer.releaseUnused());
    f.checkReleased(true);
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
    CHECK(buffer.releaseUnused());
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
    CHECK(buffer.releaseUnused());
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
    CHECK(buffer.releaseUnused());
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
    CHECK(buffer.releaseUnused());
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
    CHECK(buffer.releaseUnused());
    f.checkReleased(true);
}

TEST_CASE("GPU palette submission prevents overwrite or free until explicit retirement") {
    FakeGpu f;
    NativeGpuBuffer buffer;
    REQUIRE(buffer.create(f.api(), f.storage, 16, 3) == GpuBufferStatus::Ready);
    std::array<std::byte, 16> original{}, replacement{};
    original.fill(std::byte{0x31}); replacement.fill(std::byte{0x72});
    REQUIRE(buffer.upload(0, original) == GpuBufferStatus::Ready);
    REQUIRE(buffer.addressForSubmission(0) == f.gpu);
    CHECK(buffer.addressForSubmission(0) == f.gpu);
    CHECK(buffer.upload(0, replacement) == GpuBufferStatus::SlotInFlight);
    CHECK_FALSE(buffer.releaseUnused());
    CHECK_FALSE(f.called("finalize"));
    CHECK_FALSE(f.called("pool-finalize"));
    REQUIRE(buffer.upload(1, replacement) == GpuBufferStatus::Ready);
    REQUIRE(buffer.addressForSubmission(1) == f.gpu + 256);
    CHECK(buffer.retireAfterGpuFence(1));
    CHECK_FALSE(buffer.releaseUnused());
    CHECK(buffer.upload(0, replacement) == GpuBufferStatus::SlotInFlight);
    CHECK(buffer.retireAfterGpuFence(0));
    CHECK(buffer.addressForSubmission(0) == 0);
    REQUIRE(buffer.upload(0, replacement) == GpuBufferStatus::Ready);
    CHECK(std::equal(replacement.begin(), replacement.end(), f.storage.begin()));
    CHECK(buffer.releaseUnused());
    f.checkReleased(true);
    CHECK(buffer.addressForSubmission(0) == 0);
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
    CHECK(buffer.releaseUnused());
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
    CHECK(buffer.releaseUnused());
    f.checkReleased(true);
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
    CHECK(buffer.releaseUnused());
    f.checkReleased(true);
}
