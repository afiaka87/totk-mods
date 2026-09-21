// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include <doctest.h>
#include <array>
#include <cstring>
#include <limits>
#include <map>

#include "../src/engine/ArrowIdentity.hpp"
#include "../src/engine/ActorReference.hpp"
#include "../src/engine/CarrierSlotAppearance.hpp"
#include "../src/engine/PouchSelection.hpp"
#include "../src/engine/RayQueryExclusion.hpp"
#include "CarrierScan.hpp"

namespace {
struct IdentityFixture {
    // Synthetic addresses; distinct shooter and attachment paths match the verified native layout.
    static constexpr std::uintptr_t controller = 0x1000, arrow = 0x2000;
    static constexpr std::uintptr_t shootable = 0x3000, player = 0x4000, enemy = 0x5000;
    static constexpr std::uintptr_t attachmentLink = 0x6000, attachment = 0x7000;
    std::map<std::uintptr_t, std::uintptr_t> pointers{
        {controller + 24, arrow}, {controller + 80, shootable},
        {shootable + 112, 0xDEAD}, // Embedded link contents are NOT a pointer to another link.
        {shootable + 304, 0},
    };
    std::map<std::uintptr_t, std::uintptr_t> actors{{shootable + 112, player},
                                                {attachmentLink, attachment}};
    unsigned reads = 0, resolves = 0;

    arrowbound::engine::ArrowIdentity resolve(std::uintptr_t candidate = controller) {
        return arrowbound::engine::resolveArrowIdentity(candidate,
            [](std::uintptr_t value) { return value >= 0x1000 && value < 0x8000; },
            [&](std::uintptr_t address) {
                ++reads;
                const auto found = pointers.find(address);
                CHECK(found != pointers.end());
                return found == pointers.end() ? 0 : found->second;
            },
            [&](std::uintptr_t link) {
                ++resolves;
                CHECK(link == shootable + 112);
                const auto found = actors.find(link);
                return found == actors.end() ? 0 : found->second;
            });
    }
};
}

TEST_CASE("unfused player arrow uses its embedded shooter link") {
    IdentityFixture f;
    const auto identity = f.resolve();
    CHECK(identity.actor == f.arrow);
    CHECK(identity.shootable == f.shootable);
    CHECK(identity.belongsTo(f.player));
    CHECK_FALSE(identity.belongsTo(f.enemy));
    CHECK(f.reads == 2);
    CHECK(f.resolves == 1);
}

TEST_CASE("fused attachment cannot replace or impersonate the shooter") {
    IdentityFixture f;
    f.pointers[f.shootable + 304] = f.attachmentLink;
    CHECK(f.resolve().belongsTo(f.player));
    f.actors[f.shootable + 112] = f.enemy;
    f.actors[f.attachmentLink] = f.player;
    CHECK_FALSE(f.resolve().belongsTo(f.player));
    CHECK(f.resolve().belongsTo(f.enemy));
}

TEST_CASE("invalid or unresolved projectile identity never grants player ownership") {
    IdentityFixture f;
    CHECK_FALSE(f.resolve(0).belongsTo(f.player));
    CHECK(f.reads == 0);
    CHECK(f.resolves == 0);
    f.pointers[f.controller + 80] = 0;
    CHECK_FALSE(f.resolve().belongsTo(f.player));
    CHECK(f.resolves == 0);
    f.pointers[f.controller + 80] = f.shootable;
    f.actors[f.shootable + 112] = 0;
    CHECK_FALSE(f.resolve().belongsTo(0));
    CHECK_FALSE(f.resolve().belongsTo(f.player));
    f.pointers[f.controller + 24] = 0;
    const auto previous = f.resolves;
    CHECK_FALSE(f.resolve().belongsTo(f.player));
    CHECK(f.resolves == previous);
}

TEST_CASE("carrier grant requires a complete readable absence scan") {
    using namespace arrowbound::pure;
    const auto matches = [](const char* name) { return std::strcmp(name, "carrier") == 0; };
    std::array<const char*, 3> names{{"other", nullptr, "carrier"}};
    const auto read = [&](std::uint32_t i) { return names.at(i); };
    CHECK(scanCarrier(3, read, matches) == CarrierPresence::Unknown);
    names[1] = "other";
    CHECK(scanCarrier(3, read, matches) == CarrierPresence::Present);
    names[2] = "";
    CHECK(scanCarrier(3, read, matches) == CarrierPresence::Absent);
    CHECK(scanCarrier(0, read, matches) == CarrierPresence::Unknown);
    CHECK(scanCarrier(std::numeric_limits<std::uint32_t>::max(), read, matches) ==
          CarrierPresence::Unknown);
    CHECK(scanCarrier(513, read, matches) == CarrierPresence::Unknown);
    // A cached absence during load must not authorize a later grant.
    names[2] = "carrier";
    CHECK(scanCarrier(3, read, matches) == CarrierPresence::Present);
}

TEST_CASE("emblem appearance is scoped to its own slot and restored after rendering") {
    using arrowbound::engine::CarrierSlotAppearance;
    std::array<unsigned char, 96> options{};
    options.fill(0xA5);
    const char* carrier = "Obj_CaveWellHonor_00";
    std::memcpy(options.data(), &carrier, sizeof(carrier));
    const auto original = options;
    for (bool enabled : {false, true}) {
        {
            CarrierSlotAppearance appearance(options.data(), false, enabled);
            CHECK(options == original); // Every unrelated item, including sage vows, stays native.
        }
        {
            CarrierSlotAppearance appearance(options.data(), true, enabled);
            CHECK(options[8] == (enabled ? 1 : 0));
            std::uint32_t state = 0;
            std::memcpy(&state, options.data() + 16, sizeof(state));
            CHECK(state == (enabled ? 1u : 4u));
            const char* rendered = nullptr;
            std::memcpy(&rendered, options.data(), sizeof(rendered));
            CHECK(std::strcmp(rendered, "NormalArrow") == 0);
            for (std::size_t i = 0; i < options.size(); ++i) {
                if (i >= 80 && i < 88) CHECK(options[i] == 0);
                else if (i >= 8 && i != 8 && (i < 16 || i >= 20)) CHECK(options[i] == original[i]);
            }
        }
        CHECK(options == original);
    }
    CarrierSlotAppearance nullOptions(nullptr, false, true);
}

TEST_CASE("missing emblem is granted from a ready inventory without selecting any menu item") {
    using namespace arrowbound::pure;
    const auto read = [](std::uint32_t) { return "Obj_ProofKorok"; };
    const auto matches = [](const char*) { return false; };
    const auto loading = scanCarrier(0, read, matches);
    const auto missing = scanCarrier(1, read, matches);
    CHECK_FALSE(carrierGrantDue(loading, 100, 0, 300));
    REQUIRE(missing == CarrierPresence::Absent);
    CHECK(carrierGrantDue(missing, 100, 0, 300));
    CHECK_FALSE(carrierGrantDue(missing, 399, 100, 300));
    CHECK(carrierGrantDue(missing, 400, 100, 300));
    CHECK_FALSE(carrierGrantDue(CarrierPresence::Present, 400, 100, 300));
    CHECK_FALSE(carrierGrantDue(CarrierPresence::Unknown, 400, 100, 300));
}

TEST_CASE("selected emblem refresh resolves the current UI page without a retained slot") {
    std::map<std::uintptr_t, std::uintptr_t> pointers{
        {0x1060, 0x2000}, {0x2000, 0x3000}, {0x3000, 0x4000}, {0x3008, 0x5000},
        {0x2008, 0x6000}, {0x6048, 0x7000}, {0x7010, 0x8000}, {0x3048, 0x9000},
        {0x9010, 0xA000}};
    std::map<std::uintptr_t, std::uint32_t> integers{
        {0x5030, 5}, {0x5034, 4}, {0x1058, 2}, {0x6040, 20}, {0x3040, 20}};
    const auto valid = [](std::uintptr_t p) { return p >= 0x1000 && p <= 0xA000; };
    // first+0 is its vtable, NOT another layout pointer. Its +8 intentionally has no entry.
    const auto pointer = [&](std::uintptr_t p) { return pointers.at(p); };
    const auto integer = [&](std::uintptr_t p) { return integers.at(p); };
    const auto slot = [&](unsigned index) {
        return arrowbound::engine::selectedKeyItemSlot(0x1000, index, valid, pointer, integer);
    };
    CHECK(slot(2) == 0x8000);
    CHECK(slot(22) == 0xA000); // Recycled UI page, current index determines the actual slot.
    pointers[0x7010] = 0;
    CHECK(slot(2) == 0);
    integers[0x1058] = 0;
    CHECK(slot(2) == 0);
}

TEST_CASE("impact exclusion owns its filter and never mutates a donor or retains a pointer") {
    std::array<unsigned char, 0x200> query{};
    alignas(8) std::array<unsigned char, 0x200> donor{};
    donor.fill(0xA5);
    const auto address = reinterpret_cast<std::uintptr_t>(donor.data() + 0x128);
    std::memcpy(donor.data() + 96, &address, sizeof(address));
    const auto original = donor;
    query = donor;
    REQUIRE(arrowbound::engine::rebaseRayFilter(query.data(), donor.data()));
    std::uintptr_t relocated = 0;
    std::memcpy(&relocated, query.data() + 96, sizeof(relocated));
    CHECK(relocated == reinterpret_cast<std::uintptr_t>(query.data() + 0x128));
    auto* filter = reinterpret_cast<unsigned char*>(relocated);
    filter[8] = 1;
    filter[20] = 2;
    query[0x134] = 3;
    CHECK(filter[12] == 3); // Native layer and group writes address the same copied filter.
    CHECK(donor == original);
    CHECK_FALSE(arrowbound::engine::rebaseRayFilter(query.data(), donor.data()));
}

TEST_CASE("native shooter reference releases only its acquired positive count") {
    struct alignas(8) ActorFixture {
        unsigned char unused[432]{};
        std::int32_t references = 3;
        std::uint32_t padding = 0;
    } actor;
    static_assert(offsetof(ActorFixture, references) == 432);
    for (bool owns : {false, true}) {
        {
            arrowbound::engine::ActorReference reference;
            reference.actor = reinterpret_cast<std::uintptr_t>(&actor);
            reference.owns = owns;
        }
        CHECK(actor.references == (owns ? 2 : 3));
    }
    for (int count : {-1, 0}) {
        actor.references = count;
        {
            arrowbound::engine::ActorReference reference;
            reference.actor = reinterpret_cast<std::uintptr_t>(&actor);
            reference.owns = true;
        }
        CHECK(actor.references == count);
    }
    arrowbound::engine::ActorReference empty;
    empty.owns = true;
}
