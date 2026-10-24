// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "StasisEquipment.hpp"
#include <array>
#include <cstring>

TEST_CASE("equipment lookup respects the actor's allocated component table") {
    alignas(8) std::array<unsigned char, 560> actor{};
    alignas(8) std::array<std::uintptr_t, 147> table{};
    alignas(8) std::array<std::uintptr_t, 4> component{};
    const auto address = reinterpret_cast<std::uintptr_t>(actor.data());
    const auto tableAddress = reinterpret_cast<std::uintptr_t>(table.data());
    const auto componentAddress = reinterpret_cast<std::uintptr_t>(component.data());
    std::memcpy(actor.data() + 552, &tableAddress, 8);
    table[146] = componentAddress;
    component[3] = address;
    unsigned tableReads = 0;
    auto read = [&]<class T>(std::uintptr_t p) {
        if (p >= tableAddress && p < tableAddress + sizeof(table)) ++tableReads;
        return totk::engine::readMemory<T>(p);
    };
    auto setCount = [&](int count) { std::memcpy(actor.data() + 544, &count, 4); };
    SUBCASE("short table never reads the plausible pointer beyond its end") {
        setCount(66);
        CHECK(stasis::equipment::find(address,146,read).address == 0);
        CHECK(tableReads == 0);
    }
    SUBCASE("last allocated slot retains a matching component") {
        setCount(147);
        CHECK(stasis::equipment::find(address,146,read).address == componentAddress);
        CHECK(tableReads == 1);
    }
    SUBCASE("invalid owner never reaches the native ownership function") {
        setCount(147);
        component[3] = 1;
        const auto result = stasis::equipment::find(address,146,read);
        CHECK(result.address == 0);
        CHECK(result.error == 4);
        CHECK(result.detail == 1);
    }
    SUBCASE("corrupt count is refused before any table access") {
        setCount(-1);
        CHECK(stasis::equipment::find(address,146,read).error == 1);
        CHECK(tableReads == 0);
    }
}

TEST_CASE("player equipment IDs exclude carried gear but not a dropped actor") {
    alignas(8) std::array<unsigned char, 512> user{};
    user.fill(0xff);
    const auto address=reinterpret_cast<std::uintptr_t>(user.data());
    auto put=[&](unsigned offset,std::uint32_t id) { std::memcpy(user.data()+offset,&id,4); };
    auto read=[]<class T>(std::uintptr_t p) { return totk::engine::readMemory<T>(p); };
    put(48,10);put(48+24*7,20);put(240,30);put(240+24*11,40);
    for (auto id:{10u,20u,30u,40u}) CHECK(stasis::equipment::contains(address,id,read));
    CHECK_FALSE(stasis::equipment::contains(address,50,read));
    CHECK_FALSE(stasis::equipment::contains(address,UINT32_MAX,read));
    put(48,UINT32_MAX); // Unequipping removes ownership without a weapon-name blacklist.
    CHECK_FALSE(stasis::equipment::contains(address,10,read));
}
