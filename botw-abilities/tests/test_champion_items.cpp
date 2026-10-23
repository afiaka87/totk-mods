// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "KeyItemUI.hpp"
#include <array>

using namespace botw_abilities::champions;
namespace {
struct Key { const char* actor; };
constexpr std::array<Key, 4> keys{{{"Obj_BotwHeroSoul_Zora"}, {"Obj_BotwHeroSoul_Rito"},
                                 {"Obj_BotwHeroSoul_Goron"}, {"Obj_BotwHeroSoul_Gerudo"}}};
States full() { return {initial(Blessing::Mipha), initial(Blessing::Revali),
                       initial(Blessing::Daruk), initial(Blessing::Urbosa)}; }
}

TEST_CASE("Champion menu actions leave Sage vows and the Arrowbound Emblem alone") {
    for (const char* actor : {"Obj_SageSoul_Rito", "Obj_SageSoulPlus_Goron", "Obj_CaveWellHonor_00",
                             "Obj_BotwHeroSoul_RitoPlus", "Obj_BotwHeroSoul_Rit", ""}) {
        CHECK(keyItemIndex(keys, actor) == -1);
        CHECK_FALSE((ItemSelection{keyItemIndex(keys, actor), 0}.owns(15)));
    }
    CHECK(keyItemIndex(keys, nullptr) == -1);
    for (std::size_t i = 0; i < kBlessingCount; ++i) {
        SavedState state;
        REQUIRE(state.restore(0, full()));
        const ItemSelection selection{keyItemIndex(keys, keys[i].actor), 0};
        REQUIRE(selection.index == static_cast<int>(i));
        CHECK(selection.owns(15)); CHECK(selection.owns(16));
        CHECK_FALSE(selection.owns(-1)); CHECK_FALSE(selection.owns(17));
        REQUIRE(state.enable(selection.generation, static_cast<Blessing>(selection.index), false));
        const auto result = state.snapshot();
        for (std::size_t j = 0; j < kBlessingCount; ++j) CHECK(result.states[j].enabled == (j != i));
        const auto generation = state.invalidate();
        REQUIRE(state.restore(generation, full()));
        CHECK_FALSE(state.enable(selection.generation, static_cast<Blessing>(selection.index), false));
    }
}

TEST_CASE("Champion slot appearance preserves its icon and every unrelated native option") {
    for (bool active : {false, true}) {
        std::array<unsigned char, 96> options{};
        for (std::size_t i = 0; i < options.size(); ++i) options[i] = static_cast<unsigned char>(i + 30);
        const auto before = options;
        {
            const ItemAppearance scope(options.data(), active);
            CHECK(options[8] == (active ? 1 : 0));
            std::uint32_t state{};
            std::memcpy(&state, options.data() + 16, sizeof(state));
            CHECK(state == (active ? 1 : 4));
            for (std::size_t i = 0; i < options.size(); ++i)
                if (i != 8 && (i < 16 || i >= 20)) CHECK(options[i] == before[i]);
        }
        CHECK(options == before);
    }
}

TEST_CASE("Champion grants distinguish empty pouch slots from incomplete GameData") {
    std::array<const char*, 200> slots;
    slots.fill("");
    slots[1] = "Obj_SageSoul_Rito";
    slots[150] = keys[0].actor;
    const auto read = [&](std::uint32_t index) { return slots[index]; };
    auto scan = scanKeyItems(keys, 200, read);
    CHECK(scan.complete);
    CHECK(scan.found == 1);
    for (std::size_t i = 1; i < keys.size(); ++i) slots[i + 150] = keys[i].actor;
    scan = scanKeyItems(keys, 200, read);
    CHECK(scan.complete);
    CHECK(scan.found == 15);
    slots[199] = nullptr;
    CHECK_FALSE(scanKeyItems(keys, 200, read).complete);
    CHECK_FALSE(scanKeyItems(keys, 0, read).complete);
    CHECK_FALSE(scanKeyItems(keys, 201, read).complete);
}

TEST_CASE("A new save's empty Key Items pouch is granted only after it stays empty") {
    const auto read = [](std::uint32_t) -> const char* { return nullptr; };
    EmptyPouch empty;
    CHECK_FALSE(scanKeyItems(keys, 0, read, empty.confirmed(0)).complete);
    CHECK_FALSE(scanKeyItems(keys, 0, read, empty.confirmed(0)).complete);
    const auto scan = scanKeyItems(keys, 0, read, empty.confirmed(0));
    CHECK(scan.complete);
    CHECK(scan.found == 0);
    CHECK(empty.confirmed(0));
    CHECK_FALSE(empty.confirmed(3));
    CHECK_FALSE(empty.confirmed(0));
    CHECK_FALSE(scanKeyItems(keys, 201, read, true).complete);
    CHECK_FALSE(scanKeyItems(keys, 2, read, true).complete);
}
