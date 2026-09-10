#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

#include "RecallMonochrome.hpp"
#include "doctest.h"

using namespace self_recall::pure;

namespace {
template<class T> T read(std::span<const std::byte> bytes, std::size_t offset) {
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
template<class T> void write(std::span<std::byte> bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::uint32_t shaderAttribute(float value) {
    volatile float multiplied = value * 255.0f;
    return static_cast<std::uint32_t>(multiplied);
}
}

TEST_CASE("private monochrome filter reaches native full-strength targets without changing borrowed state") {
    std::array<std::byte, kMonochromeFilterBytes> native{}, privateFilter{};
    for (std::size_t i = 0; i < native.size(); ++i)
        native[i] = static_cast<std::byte>((i * 37u + 11u) & 0xFFu);
    write(native, 0x64, std::uint32_t{0xA57C0010});
    write(native, 0x1C, 0.15f);
    write(native, 0x58, 0.2f);
    write(native, 0x60, 4.0f);
    write(native, 0x7C, 1.0f);
    write(native, 0x80, 1.0f);
    const auto original = native;
    REQUIRE(buildMonochromeFilter(native, privateFilter) == MonochromeFilterStatus::Ready);
    CHECK(native == original);
    const auto flags = read<std::uint32_t>(privateFilter, 0x64);
    CHECK((flags & 1u) != 0);
    CHECK((flags & 0x10u) == 0);
    CHECK((flags & ~0x11u) == (read<std::uint32_t>(original, 0x64) & ~0x11u));
    CHECK(read<float>(privateFilter, 0x1C) > 0.99f);
    const auto weight = read<float>(privateFilter, 0x58) / read<float>(privateFilter, 0x60);
    CHECK(1.0f + weight * (read<float>(privateFilter, 0x7C) - 1.0f) == 0.375f);
    CHECK(1.0f + weight * (read<float>(privateFilter, 0x80) - 1.0f) == 0.25f);
    for (std::size_t i = 0; i < native.size(); ++i) {
        bool replaced = false;
        for (const auto offset : {0x1Cu, 0x58u, 0x60u, 0x64u, 0x7Cu, 0x80u})
            replaced |= i >= offset && i < offset + 4u;
        if (!replaced) CHECK(privateFilter[i] == original[i]);
    }
}

TEST_CASE("private monochrome filter refuses bad spans and overlap before any writes") {
    std::array<std::byte, kMonochromeFilterBytes + 1> source{}, destination{};
    destination.fill(std::byte{0xA6});
    const auto before = destination;
    const std::span<const std::byte> native{source.data(), kMonochromeFilterBytes};
    const std::span<std::byte> output{destination.data(), kMonochromeFilterBytes};
    CHECK(buildMonochromeFilter({}, output) == MonochromeFilterStatus::InvalidSize);
    CHECK(buildMonochromeFilter(native, {}) == MonochromeFilterStatus::InvalidSize);
    CHECK(buildMonochromeFilter(native.first(kMonochromeFilterBytes - 1), output) ==
          MonochromeFilterStatus::InvalidSize);
    CHECK(buildMonochromeFilter(native, output.first(kMonochromeFilterBytes - 1)) ==
          MonochromeFilterStatus::InvalidSize);
    CHECK(buildMonochromeFilter(source, output) == MonochromeFilterStatus::InvalidSize);
    CHECK(buildMonochromeFilter(native, destination) == MonochromeFilterStatus::InvalidSize);
    CHECK(destination == before);

    const auto original = source;
    CHECK(buildMonochromeFilter(native, {source.data(), kMonochromeFilterBytes}) ==
          MonochromeFilterStatus::OverlappingBuffers);
    CHECK(buildMonochromeFilter(native, {source.data() + 1, kMonochromeFilterBytes}) ==
          MonochromeFilterStatus::OverlappingBuffers);
    CHECK(buildMonochromeFilter({source.data() + 1, kMonochromeFilterBytes},
                               {source.data(), kMonochromeFilterBytes}) ==
          MonochromeFilterStatus::OverlappingBuffers);
    CHECK(source == original);

    std::array<std::byte, kMonochromeFilterBytes * 2> adjacent{};
    CHECK(buildMonochromeFilter({adjacent.data(), kMonochromeFilterBytes},
                               {adjacent.data() + kMonochromeFilterBytes, kMonochromeFilterBytes}) ==
          MonochromeFilterStatus::Ready);
}

TEST_CASE("monochrome exclusion preserves every decoded 16-bit shader attribute") {
    for (std::uint32_t flags = 0; flags <= 0xFFFFu; ++flags) {
        CAPTURE(flags);
        const float source = flags == 0xFFFFu ? 257.0f :
            static_cast<float>((static_cast<double>(flags) + 0.5) / 255.0);
        REQUIRE(shaderAttribute(source) == flags);
        const auto originalBits = std::bit_cast<std::uint32_t>(source);
        float excluded = -1.0f;
        REQUIRE(enableMonochromeExclusion(source, excluded) == ObjectAttributeStatus::Ready);
        CHECK(shaderAttribute(excluded) == (flags | 0x80u));
        CHECK(std::bit_cast<std::uint32_t>(source) == originalBits);
        if (flags & 0x80u)
            CHECK(std::bit_cast<std::uint32_t>(excluded) == originalBits);
    }
}

TEST_CASE("monochrome exclusion accepts shader boundaries and rejects invalid floats without publishing") {
    for (const float source : {0.0f, -0.0f, std::numeric_limits<float>::denorm_min(),
                              127.0f / 255.0f, 128.0f / 255.0f, 1.0f, 256.0f, 257.0f}) {
        CAPTURE(source);
        float excluded = -1.0f;
        REQUIRE(enableMonochromeExclusion(source, excluded) == ObjectAttributeStatus::Ready);
        CHECK(shaderAttribute(excluded) == (shaderAttribute(source) | 0x80u));
    }
    float zeroExcluded = -1.0f;
    REQUIRE(enableMonochromeExclusion(0.0f, zeroExcluded) == ObjectAttributeStatus::Ready);
    CHECK(std::bit_cast<std::uint32_t>(zeroExcluded) == 0x3F008081u);

    for (const float source : {-1.0f, -std::numeric_limits<float>::denorm_min(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN(),
                              std::nextafter(257.0f, std::numeric_limits<float>::infinity())}) {
        float excluded = 42.25f;
        CHECK(enableMonochromeExclusion(source, excluded) == ObjectAttributeStatus::InvalidValue);
        CHECK(excluded == 42.25f);
    }
}

TEST_CASE("naturally excluded appearance still refreshes each stale native buffer") {
    float source = std::bit_cast<float>(0x3FD0D0D1u);
    const float oldAppearance = std::bit_cast<float>(0x3FFF7F7Fu);
    std::array<float, 3> gpu{oldAppearance, oldAppearance, oldAppearance};
    unsigned pending = 0;
    for (unsigned buffer = 0; buffer < gpu.size(); ++buffer) {
        CAPTURE(buffer);
        float excluded = 0;
        REQUIRE(enableMonochromeExclusion(source, excluded) == ObjectAttributeStatus::Ready);
        REQUIRE(excluded == source);
        REQUIRE(gpu[buffer] != excluded);
        const auto original = source;
        uploadMonochromeAttribute(source, excluded,
            [&](float value) { source = value; pending |= 7; },
            [&] {
                if (pending & (1u << buffer)) gpu[buffer] = source;
                pending &= ~(1u << buffer);
            });
        CHECK(gpu[buffer] == excluded);
        CHECK(source == original);
    }
}

TEST_CASE("temporary monochrome upload restores CPU and schedules native GPU restoration on exit") {
    float source = 0.0f, gpu = -1.0f, excluded = -1.0f;
    bool dirty = false;
    REQUIRE(enableMonochromeExclusion(source, excluded) == ObjectAttributeStatus::Ready);
    const auto calculate = [&] { if (dirty) gpu = source; dirty = false; };
    uploadMonochromeAttribute(source, excluded,
        [&](float value) { source = value; dirty = true; }, calculate);
    CHECK(gpu == excluded);
    CHECK(source == 0.0f);
    CHECK(dirty);
    calculate(); // next native calculation after Recall releases the model
    CHECK(gpu == 0.0f);
    CHECK_FALSE(dirty);
}
