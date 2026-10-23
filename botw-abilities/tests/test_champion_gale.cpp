// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "Gale.hpp"
#include "GaleMotion.hpp"
#include "EffectPulse.hpp"
#include "WindFields.hpp"
#include <map>

using namespace botw_abilities::champions;

TEST_CASE("Gale requires an observed airborne phase before accepting landing") {
    const auto charges = initial(Blessing::Revali);
    auto state = stepGale({}, charges, {true}, {true, true, true, true}, 1.7f).state;
    for (int frame = 0; frame < 3; ++frame) {
        const auto result = stepGale(state, charges, {true}, {false, true, true, true, false, 1}, 1.7f);
        CHECK(result.state.phase == GalePhase::Jumping);
        CHECK_FALSE(result.beginChannel);
        state = result.state;
    }
    state = stepGale(state, charges, {true}, {false, true, false, false}, 1.7f).state;
    CHECK(state.airborneSeen);
    const auto landing = stepGale(state, charges, {true}, {false, true, true, true}, 1.7f);
    CHECK(landing.beginChannel);
    CHECK(landing.state.phase == GalePhase::Charging);
}

TEST_CASE("Gale lets the native jump run then owns movement through the grounded skid and channel") {
    CHECK_FALSE(holdsMovement(GalePhase::Idle));
    CHECK_FALSE(holdsMovement(GalePhase::Jumping));
    CHECK(holdsMovement(GalePhase::Settling));
    CHECK(holdsMovement(GalePhase::Charging));
    CHECK(holdsMovement(GalePhase::Ready));
    const auto charges = initial(Blessing::Revali);
    const auto stopped = stepGale({GalePhase::Settling}, charges, {true}, {false,true,true,true}, 1.7f);
    REQUIRE(stopped.beginChannel);
    CHECK(holdsMovement(stopped.state.phase));
    const auto released = stepGale(stopped.state, charges, {true}, {false,false,true,true}, 1.7f);
    CHECK_FALSE(holdsMovement(released.state.phase));
}

TEST_CASE("Held channel effects keep one activation identity until explicitly stopped") {
    CarrierState carrier;
    carrier.pulse.hold(true);
    const auto serial = carrier.pulse.serial;
    REQUIRE(observeCarrier(carrier, false).action == CarrierAction::Spawn);
    carrier.created();
    for (int frame = 0; frame < 300; ++frame) {
        carrier.pulse.hold(true);
        carrier.tick(1);
        CHECK(observeCarrier(carrier, true).action == CarrierAction::Follow);
        CHECK(carrier.pulse.serial == serial);
    }
    carrier.pulse.hold(false);
    CHECK(observeCarrier(carrier, true).action == CarrierAction::Delete);
    CHECK(observeCarrier(carrier, false).action == CarrierAction::None);
}

TEST_CASE("Updraft matching preserves distinct fields even at the same origin") {
    std::array<WindFieldOwner, 3> owners{{{true, 10, {}}, {true, 20, {}}, {true, 0, {2,0,0}}}};
    std::array<WindFieldActor, 3> actors{{{20, {}}, {10, {}}, {30, {2,0,0}}}};
    const auto matching = matchWindFields(owners, actors);
    CHECK(matching == std::array<int, 3>{1, 0, 2});
    owners[0].owned = false;
    const auto expired = matchWindFields(owners, actors);
    CHECK(expired[0] == -1);
    CHECK(expired[1] == 0);
    owners[0] = {true, 0, {100,0,0}};
    CHECK(matchWindFields(owners, actors)[0] == -1);
    owners[0] = {true, 99, {}};
    CHECK(matchWindFields(owners, actors)[0] == -1);
}

TEST_CASE("Gale checks every native character-body pointer before requesting launch velocity") {
    std::map<std::uintptr_t, std::uintptr_t> memory{
        {0x1018, 0x2000}, {0x2228, 0x3000}, {0x3050, 0x4000}, {0x4020, 0x5000},
        {0x5010, 0x6000}, {0x6018, 0x7000}, {0x7000, 0x8000}};
    const auto valid = [](std::uintptr_t address) { return address >= 0x1000; };
    const auto read = [&](std::uintptr_t address) { return memory.at(address); };
    CHECK(galeLaunchBody(0x1000, 0x2000, valid, read) == 0x8000);
    CHECK(galeLaunchBody(0x1000, 0x9000, valid, read) == 0);
    for (auto& [address, value] : memory) {
        const auto original = value;
        value = 0;
        CHECK(galeLaunchBody(0x1000, 0x2000, valid, read) == 0);
        value = original;
        (void)address;
    }
}
