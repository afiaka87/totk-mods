#include <doctest.h>
#include <array>
#include <cstring>
#include <limits>

#include "TallSpeed.hpp"

using namespace zonai_ascend::speed;
using zonai_ascend::remote::Vec3;
namespace {
template<class T> void put(void* bytes, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(bytes)+offset, &value, sizeof(value));
}
template<class T> T get(const void* bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const std::byte*>(bytes)+offset, sizeof(value));
    return value;
}
struct Fixture {
    std::array<std::byte, 0x1400> player{};
    std::array<std::byte, 0x80> node{};
    Fixture() {
        put(node.data(), 0, static_cast<void*>(player.data()));
        put(node.data(), 0x20, 1u);
        put(node.data(), 0x3C, Vec3{0, 600, 0});
        put(node.data(), 0x58, 50.0f);
        put(player.data(), 0x132C, Vec3{0, 600, 0});
        put(player.data(), 0x1344, Vec3{0, 100, 0});
        flags(kApproach);
        bind([](void* n) { return get<void*>(n, 0); });
    }
    ~Fixture() { bind(nullptr); }
    void flags(std::uint32_t value) { put(player.data(), 0xAE4, value); }
    float step(bool travel = false, float value = 0.6f) {
        return scaleForwardStep(player.data(), value, travel);
    }
};
struct TierCase { float rise, factor; };
constexpr TierCase tiers[] = {
    {0, 1}, {150, 1}, {150.01f, 1.5f}, {300, 1.5f}, {300.01f, 2},
    {500, 2}, {500.01f, 3}, {1000, 3}, {1000.01f, 6},
    {1500, 6}, {1500.01f, 7}, {2000, 7}, {2000.01f, 7.5f}, {10000, 7.5f}, {10001, 1}
};
}

TEST_CASE("speed tiers use full vertical rise and strict user-selected thresholds") {
    for (auto [rise, factor] : tiers) {
        CAPTURE(rise);
        CHECK(multiplier({0, 0, 0}, {0, rise, 0}, kApproach) == factor);
        CHECK(multiplier({0, -1000, 0}, {0, rise-1000, 0}, kTravel) == factor);
    }
    CHECK(multiplier({0, 115.84f, 0}, {0, 1531.55f, 0}, kApproach) == 6);
    CHECK(multiplier({0, 100, 0}, {0, 100, 1000}, kTravel) == 1);
    CHECK(multiplier({0, 600, 0}, {0, 100, 0}, kTravel) == 1);
}

TEST_CASE("tall speed excludes inactive preview cancellation and invalid coordinates") {
    for (auto flags : {0u, kPreview, kCancel, kApproach|kCancel, kTravel|kPreview})
        CHECK(multiplier({0, 0, 0}, {0, 1000, 0}, flags) == 1);
    const auto inf = std::numeric_limits<float>::infinity();
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(multiplier({nan, 0, 0}, {0, 1000, 0}, kTravel) == 1);
    CHECK(multiplier({0, 0, 0}, {0, inf, 0}, kTravel) == 1);
    CHECK(multiplier({0, 0, 0}, {0, 1000, nan}, kTravel) == 1);
    CHECK(scalePositive(0, 2.5f) == 0);
    CHECK(scalePositive(-1, 2.5f) == -1);
    CHECK(scalePositive(inf, 2.5f) == inf);
    CHECK(scalePositive(std::numeric_limits<float>::max(), 2.5f) == std::numeric_limits<float>::max());
}

TEST_CASE("approach boost is confined to the borrowed state1 physics call") {
    Fixture f;
    CHECK(f.step() == 0.6f);
    {
        ApproachScope scope(f.node.data());
        CHECK(f.step() == doctest::Approx(1.2f));
        auto other = f.player;
        CHECK(scaleForwardStep(other.data(), 0.6f, false) == 0.6f);
        {
            ApproachScope nested(nullptr);
            CHECK(f.step() == 0.6f);
        }
        CHECK(f.step() == doctest::Approx(1.2f));
    }
    CHECK(f.step() == 0.6f);
    for (unsigned state : {0u, 2u, 3u}) {
        put(f.node.data(), 0x20, state);
        ApproachScope scope(f.node.data());
        CHECK(f.step() == 0.6f);
        CHECK(scaleApproachLookahead(f.node.data(), 0.6f) == 0.6f);
    }
}

TEST_CASE("approach lookahead and movement advance by the same boosted step") {
    Fixture f;
    ApproachScope scope(f.node.data());
    for (auto [rise, factor] : tiers) {
        put(f.player.data(), 0x1344, Vec3{0, 0, 0});
        put(f.player.data(), 0x132C, Vec3{0, rise, 0});
        CHECK(f.step() == doctest::Approx(0.6f*factor));
        CHECK(scaleApproachLookahead(f.node.data(), 0.6f) == doctest::Approx(f.step()));
    }
    put(f.player.data(), 0x132C, Vec3{0, 2500, 0});
    for (auto flags : {kApproach|kCancel, kApproach|kPreview, 0u}) {
        f.flags(flags);
        CHECK(f.step() == 0.6f);
        CHECK(scaleApproachLookahead(f.node.data(), 0.6f) == 0.6f);
    }
}

TEST_CASE("travel boosts only its own call and uses the native capped output") {
    Fixture f;
    f.flags(kTravel);
    CHECK(f.step(false) == 0.6f);
    CHECK(f.step(true) == doctest::Approx(1.2f));
    CHECK(f.step(true, 1.852f) == doctest::Approx(3.704f));
    for (auto flags : {kTravel|kCancel, kTravel|kPreview, 0u}) {
        f.flags(flags);
        CHECK(f.step(true) == 0.6f);
    }
    f.flags(kTravel);
    put(f.player.data(), 0x132C, Vec3{0, 250, 0});
    CHECK(f.step(true) == 0.6f);
    put(f.player.data(), 0x132C, Vec3{0, -500, 0});
    CHECK(f.step(true) == 0.6f);
}

TEST_CASE("braking radius expands after native recomputation with no near-exit slope increase") {
    Fixture f;
    f.flags(kTravel);
    for (auto [rise, factor] : tiers) for (float radius : {5.0f, 50.0f}) {
        put(f.player.data(), 0x1344, Vec3{0, 0, 0});
        put(f.player.data(), 0x132C, Vec3{0, rise, 0});
        put(f.node.data(), 0x3C, Vec3{0, rise, 0});
        put(f.node.data(), 0x58, radius);
        extendBraking(f.node.data());
        const float actual = get<float>(f.node.data(), 0x58);
        CHECK(actual == radius*factor);
        CHECK(actual > 1.852f*factor);
        CHECK((0.67f*factor)*(1.0f/actual) == doctest::Approx(0.67f/radius));
        put(f.node.data(), 0x58, radius); // Native calcSpeed resets it before each callback.
        extendBraking(f.node.data());
        CHECK(get<float>(f.node.data(), 0x58) == actual);
    }
}

TEST_CASE("braking excludes a stale target short ascent cancellation and an unbound adapter") {
    Fixture f;
    f.flags(kTravel);
    put(f.node.data(), 0x3C, Vec3{0, 650, 0});
    extendBraking(f.node.data());
    CHECK(get<float>(f.node.data(), 0x58) == 50);
    put(f.node.data(), 0x3C, Vec3{0, 600, 0});
    f.flags(kTravel|kCancel);
    extendBraking(f.node.data());
    CHECK(get<float>(f.node.data(), 0x58) == 50);
    f.flags(kTravel);
    put(f.player.data(), 0x1344, Vec3{0, 450, 0});
    extendBraking(f.node.data());
    CHECK(get<float>(f.node.data(), 0x58) == 50);
    bind(nullptr);
    CHECK(f.step(true) == 0.6f);
    CHECK(scaleApproachLookahead(f.node.data(), 0.6f) == 0.6f);
}
