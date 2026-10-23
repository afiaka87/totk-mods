// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "Locations.hpp"

#include <cstring>

namespace {
using namespace botw_abilities::champions;

struct Fixture {
    std::array<std::byte, 8192> bytes{};
    static constexpr std::uintptr_t manager = 128;
    static constexpr std::uintptr_t sentinel = manager + 24;

    template <class T>
    void put(std::uintptr_t address, T value) {
        REQUIRE(address + sizeof(value) <= bytes.size());
        std::memcpy(bytes.data() + address, &value, sizeof(value));
    }

    bool read(std::uintptr_t address, void* out, std::size_t length) const {
        if (address > bytes.size() || length > bytes.size() - address) return false;
        std::memcpy(out, bytes.data() + address, length);
        return true;
    }

    void name(std::uintptr_t string, std::uintptr_t text, std::string_view contents) {
        put(string, text);
        put(string + 8, static_cast<std::int32_t>(contents.size() + 1));
        std::memcpy(bytes.data() + text, contents.data(), contents.size());
        put(text + contents.size(), '\0');
    }

    Fixture(std::initializer_list<std::string_view> locations = {},
            std::string_view fallback = "HyruleField") {
        name(manager + 3400, 4000, fallback);
        put(manager + 40, static_cast<std::int32_t>(locations.size()));
        std::uintptr_t previous = sentinel;
        unsigned index = 0;
        for (const auto location : locations) {
            const auto node = 5000 + index * 128;
            name(node, 6500 + index * 64, location);
            put(node + 96, previous);
            previous = node + 88;
            ++index;
        }
        put(manager + 32, previous);
    }

    LocationResult sample() const {
        return castleFromLocations(manager, [this](auto address, void* out, auto length) {
            return read(address, out, length);
        });
    }
};
}

TEST_CASE("Castle recharge uses current castle areas rather than nearby names") {
    for (const auto location : kCastleLocations) CHECK(castleLocation(location));
    for (const auto location : {"HyruleField", "HyruleCastleTown", "DeepHole_HyruleCastle",
                                "DeepHole_HyruleCastleEast", "HyruleCastle_Room_4", ""})
        CHECK_FALSE(castleLocation(location));
}

TEST_CASE("Castle area survives a smaller location occupying the current-location slot") {
    Fixture fixture({"HyruleCastle", "DeepHole_HyruleCastle"});
    const auto result = fixture.sample();
    REQUIRE(result);
    CHECK(result.castle);
    CHECK_FALSE(Fixture({"HyruleField", "DeepHole_HyruleCastle"}).sample().castle);
}

TEST_CASE("Castle fallback and an empty active list follow native location behavior") {
    Fixture fixture({}, "LargeDungeonHyruleCastle");
    REQUIRE(fixture.sample());
    CHECK(fixture.sample().castle);
    Fixture outside;
    REQUIRE(outside.sample());
    CHECK_FALSE(outside.sample().castle);
}

TEST_CASE("Broken or changing location lists cannot leave a stale castle bonus") {
    Fixture fixture({"HyruleCastle"});
    SUBCASE("early sentinel") { fixture.put(Fixture::manager + 40, std::int32_t{2}); }
    SUBCASE("cycle") { fixture.put(5096, std::uintptr_t{5088}); }
    SUBCASE("negative count") { fixture.put(Fixture::manager + 40, std::int32_t{-1}); }
    SUBCASE("unreadable entry") { fixture.put(Fixture::manager + 32, std::uintptr_t{9000}); }
    const auto result = fixture.sample();
    CHECK_FALSE(result);
    CHECK_FALSE(result.castle);
}

TEST_CASE("Location reader rejects missing and unterminated strings without writing native memory") {
    Fixture fixture({"HyruleCastle"});
    SUBCASE("missing") { fixture.put(5000, std::uintptr_t{0}); }
    SUBCASE("no terminator") { fixture.put(5008, std::int32_t{12}); }
    const auto before = fixture.bytes;
    CHECK_FALSE(fixture.sample());
    CHECK(fixture.bytes == before);
}
