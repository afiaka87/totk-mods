#include <array>
#include <cstring>
#include "RecallEquipmentLinks.hpp"
#include "doctest.h"

using namespace self_recall::model;

TEST_CASE("fused attachment ownership follows collected equipment but rejects foreign parents") {
    int player, sword, shield, droppedWeapon;
    std::array<const void*, 2> owned{&sword, &shield};
    CHECK(isEquipmentParent(&player, &player, {}));
    CHECK(isEquipmentParent(&sword, &player, owned));
    CHECK(isEquipmentParent(&shield, &player, owned));
    CHECK_FALSE(isEquipmentParent(&droppedWeapon, &player, owned));
    CHECK_FALSE(isEquipmentParent(nullptr, &player, owned));
    CHECK_FALSE(isEquipmentParent(&sword, &player, std::span(owned).subspan(1)));
}

TEST_CASE("equipment traversal includes all static clothing and sheathed links") {
    std::array<std::byte, 0x3EA8> equipment{};
    std::array<unsigned, 4> counts{};
    unsigned visited = 0;
    for (unsigned i = 0; i < 20; ++i) {
        const unsigned id = 100 + i;
        std::memcpy(equipment.data() + 0x20 + 0x18 * i + 0x10, &id, sizeof(id));
    }
    CHECK(visitEquipmentLinks(equipment.data(), [&](const void* link, EquipmentLinkKind kind, unsigned slot) {
        ++visited;
        ++counts[static_cast<unsigned>(kind)];
        if (kind == EquipmentLinkKind::Dynamic || kind == EquipmentLinkKind::Static) {
            unsigned id;
            std::memcpy(&id, static_cast<const std::byte*>(link) + 0x10, sizeof(id));
            CHECK(id == 100 + slot + (kind == EquipmentLinkKind::Static ? 8 : 0));
        }
        return true;
    }));
    CHECK(counts == std::array<unsigned, 4>{8, 12, 8, 1});
    CHECK(visited + 3 + 8 == kOwnedActorLimit); // Player, parasail, fairy, eight active fuses.
    visited = 0;
    CHECK_FALSE(visitEquipmentLinks(equipment.data(), [&](const void*, EquipmentLinkKind, unsigned) {
        return ++visited < 11;
    }));
    CHECK(visited == 11);
}

TEST_CASE("clothing can bind to Player through either native bone binding component") {
    std::array<std::byte, 0x428> registry{};
    std::array<std::byte, 0x88> bind{}, sameBone{};
    const auto readPointer = [](const void* p, std::size_t offset) {
        const void* value;
        std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
        return value;
    };
    const auto setPointer = [&](unsigned offset, const void* value) {
        std::memcpy(registry.data() + offset, &value, sizeof(value));
    };
    const void* expected = bind.data() + 0x70;
    const auto matches = [&](const void* link) { return link == expected; };
    CHECK_FALSE(hasBoundEquipmentParent(registry.data(), readPointer, matches));
    setPointer(0x18, bind.data());
    CHECK(hasBoundEquipmentParent(registry.data(), readPointer, matches));
    expected = sameBone.data() + 0x70;
    CHECK_FALSE(hasBoundEquipmentParent(registry.data(), readPointer, matches));
    setPointer(0x420, sameBone.data());
    CHECK(hasBoundEquipmentParent(registry.data(), readPointer, matches));
    CHECK_FALSE(hasBoundEquipmentParent(nullptr, readPointer, matches));
}

TEST_CASE("active fused equipment uses the weapon link rather than the player creation cache") {
    std::array<std::byte, 0x210> registry{};
    std::array<std::byte, 0x510> weapon{};
    std::array<std::byte, 0x3EA8> playerEquipment{};
    const auto readPointer = [](const void* p, std::size_t offset) {
        const void* value;
        std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
        return value;
    };
    const auto readByte = [](const void* p, std::size_t offset) {
        return static_cast<const std::uint8_t*>(p)[offset];
    };
    CHECK(fusedEquipmentLink(nullptr, readPointer, readByte) == nullptr);
    CHECK(fusedEquipmentLink(registry.data(), readPointer, readByte) == nullptr);
    const void* component = weapon.data();
    std::memcpy(registry.data() + 0x208, &component, sizeof(component));
    CHECK(fusedEquipmentLink(registry.data(), readPointer, readByte) == nullptr);
    weapon[0x50C] = std::byte{1};
    const auto* link = fusedEquipmentLink(registry.data(), readPointer, readByte);
    CHECK(link == weapon.data() + 0xB0);
    CHECK(link != playerEquipment.data() + 0x580);
    CHECK(link != playerEquipment.data() + 0x3E90);
    weapon[0x50C] = std::byte{0};
    CHECK(fusedEquipmentLink(registry.data(), readPointer, readByte) == nullptr);
}
