#include <array>
#include <limits>

#include "RecallNativeSceneMaterial.hpp"
#include "RecallPaletteWrites.hpp"
#include "doctest.h"

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

TEST_CASE("palette variants copy native GPU bytes and change only two resolved float fields") {
    Fixture fixture;
    SceneMaterialView view;
    REQUIRE(fixture.describe(view) == MaterialStatus::Ready);
    const auto original = fixture.gpu;
    const auto originalSource = fixture.source;
    std::array<std::byte, 640> world{}, link{};
    REQUIRE(copyPaletteVariant(view, fixture.gpu, world, {0.375f, 0.25f}) == MaterialStatus::Ready);
    REQUIRE(copyPaletteVariant(view, fixture.gpu, link, {1.0f, 1.0f}) == MaterialStatus::Ready);
    CHECK(detail::read<float>(world.data(), 256) == 0.375f);
    CHECK(detail::read<float>(world.data(), 300) == 0.25f);
    CHECK(detail::read<float>(link.data(), 256) == 1.0f);
    CHECK(detail::read<float>(link.data(), 300) == 1.0f);
    for (unsigned i = 0; i < 640; ++i) {
        if ((i >= 256 && i < 260) || (i >= 300 && i < 304)) continue;
        CHECK(world[i] == original[i]);
        CHECK(link[i] == original[i]);
    }
    CHECK(fixture.gpu == original);
    CHECK(fixture.source == originalSource);
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

TEST_CASE("palette copy validates all ranges before touching a buffer and cannot edit native input") {
    Fixture fixture;
    SceneMaterialView view;
    REQUIRE(fixture.describe(view) == MaterialStatus::Ready);
    std::array<std::byte, 640> destination{};
    destination.fill(std::byte{0xB7});
    const auto before = destination;
    CHECK(copyPaletteVariant(view, fixture.gpu, fixture.gpu, {1, 1}) == MaterialStatus::OverlappingBuffers);
    CHECK(copyPaletteVariant(view, fixture.gpu, destination, {1.1f, 1}) == MaterialStatus::InvalidSaturation);
    CHECK(copyPaletteVariant(view, fixture.gpu, destination, {1, std::numeric_limits<float>::infinity()}) == MaterialStatus::InvalidSaturation);
    CHECK(copyPaletteVariant(view, {fixture.source.data(), fixture.source.size()}, destination, {1, 1}) == MaterialStatus::InvalidBufferSize);
    view.other.uniformOffset = 639;
    CHECK(copyPaletteVariant(view, fixture.gpu, destination, {1, 1}) == MaterialStatus::UniformRange);
    CHECK(destination == before);
}

TEST_CASE("native scene-material cache distinguishes full-color Link and restores other draws") {
    constexpr std::uint32_t locations = 0xFFFF0302;
    const auto tag = fullColorCacheTag(locations);
    REQUIRE(tag != 0);
    auto cached = locations;
    CHECK(sceneMaterialComparison(cached, locations, false) == locations);
    CHECK(sceneMaterialComparison(cached, locations, true) != locations);
    cached = locations | tag;
    CHECK(sceneMaterialComparison(cached, locations, true) == locations);
    CHECK(sceneMaterialComparison(cached, locations, false) != locations);
    cached = locations;
    CHECK(sceneMaterialComparison(cached, locations, false) == locations);
    CHECK(sceneMaterialComparison(cached, locations, true) != locations);
    CHECK(sceneMaterialComparison(locations | tag, 0xFFFF0402, true) != 0xFFFF0402);
    CHECK(fullColorCacheTag(0xFFFFFFFF) == 0);
    CHECK(fullColorCacheTag(0x7F7F7F7F) == 0);
    CHECK(fullColorCacheTag(0xFFFF0382) == 0);
    CHECK(fullColorCacheTag(0x02FFFFFF) == 0x80000000);
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
    put(fixture.material, 0x2D, std::uint8_t{1}); // A different buffer can remain dirty.
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
