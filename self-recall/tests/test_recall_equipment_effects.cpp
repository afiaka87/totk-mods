#include "RecallEquipmentEffectState.hpp"
#include "RecallNativeEffectSchema.hpp"
#include <memory>
#include "doctest.h"
using namespace self_recall::pure;

TEST_CASE("effect schemas own names and enum entries after source equipment disappears") {
    using namespace self_recall::equipment_effects::detail;
    auto copy = std::make_unique<NativeEffectSchema>();
    {
        alignas(8) std::byte instance[0x60]{}, user[0x50]{}, definition[112]{};
        char userName[] = "TestWeapon";
        char propertyName[] = "DrawState";
        char firstName[] = "InHand", secondName[] = "OnBack";
        EnumValue entries[8]{{firstName, 3, 0}, {secondName, 17, 0}};
        const void* definitions[]{definition};
        write<const void*>(instance, 0x58, user);
        write<const char*>(user, 0x10, userName);
        write<std::uint16_t>(user, 0x44, 1);
        write<const void*>(user, 0x48, definitions);
        write<const char*>(definition, 8, propertyName);
        write<std::uint32_t>(definition, 16, 64);
        write<unsigned>(definition, 0x58, 0); // Proven native enum layout.
        write<int>(definition, 96, 2);
        write<int>(definition, 100, 8);
        write<const void*>(definition, 104, entries);
        REQUIRE(copySchema(*copy, instance));
        CHECK(copy->userName != userName);
        CHECK(copy->definitionPointers[0] != definition);
        CHECK(read<const void*>(copy->definitions[0], 8) != propertyName);
        CHECK(read<const void*>(copy->definitions[0], 104) != entries);
        std::memset(userName, 'X', sizeof(userName) - 1);
        std::memset(propertyName, 'Y', sizeof(propertyName) - 1);
        std::memset(firstName, 'Z', sizeof(firstName) - 1);
        entries[1].value = 999;
    }
    CHECK(std::strcmp(copy->userName, "TestWeapon") == 0);
    CHECK(std::strcmp(read<const char*>(copy->definitions[0], 8), "DrawState") == 0);
    const auto* owned = read<const EnumValue*>(copy->definitions[0], 104);
    CHECK(std::strcmp(owned[0].name, "InHand") == 0);
    CHECK(std::strcmp(owned[1].name, "OnBack") == 0);
    CHECK(owned[0].value == 3);
    CHECK(owned[1].value == 17);
    CHECK(read<int>(copy->definitions[0], 96) == 2);
    CHECK(read<int>(copy->definitions[0], 100) == 2);
}

TEST_CASE("selected loops wake after native sleep and resume without restarting valid events") {
    CHECK(planEquipmentLoop(true, false, true) == EquipmentLoopAction::WakeAndEmit);
    CHECK(planEquipmentLoop(true, true, true) == EquipmentLoopAction::WakeAndEmit);
    CHECK(planEquipmentLoop(true, false, false) == EquipmentLoopAction::Emit);
    CHECK(planEquipmentLoop(true, true, false) == EquipmentLoopAction::Keep);
    CHECK(planEquipmentLoop(false, true, false) == EquipmentLoopAction::Kill);
    CHECK(planEquipmentLoop(false, true, true) == EquipmentLoopAction::Kill);
    CHECK(planEquipmentLoop(false, false, false) == EquipmentLoopAction::Keep);
    CHECK(planEquipmentLoop(false, false, true) == EquipmentLoopAction::Keep);
}

TEST_CASE("effect schemas reject excess counts and unknown native property types") {
    using namespace self_recall::equipment_effects::detail;
    auto copy = std::make_unique<NativeEffectSchema>();
    alignas(8) std::byte instance[0x60]{}, user[0x50]{}, definition[112]{};
    const void* definitions[]{definition};
    write<const void*>(instance, 0x58, user);
    write<const char*>(user, 0x10, "TestWeapon");
    write<std::uint16_t>(user, 0x44, kProperties + 1);
    CHECK_FALSE(copySchema(*copy, instance));
    write<std::uint16_t>(user, 0x44, 1);
    write<const void*>(user, 0x48, definitions);
    write<const char*>(definition, 8, "State");
    write<unsigned>(definition, 0x58, 6);
    CHECK_FALSE(copySchema(*copy, instance));
    write<unsigned>(definition, 0x58, 0);
    write<int>(definition, 96, kEnums + 1);
    write<int>(definition, 100, kEnums + 1);
    CHECK_FALSE(copySchema(*copy, instance));
    write<int>(definition, 96, 1);
    write<int>(definition, 100, 1);
    CHECK_FALSE(copySchema(*copy, instance)); // Missing enum data.
    write<int>(definition, 96, 0);
    CHECK(copySchema(*copy, instance)); // Unused capacity owns no entries.
    write<int>(definition, 96, 1);
    write<int>(definition, 100, 0);
    CHECK_FALSE(copySchema(*copy, instance)); // Size cannot exceed capacity.
}

TEST_CASE("scalar effect property definitions retain their distinct native range fields") {
    using namespace self_recall::equipment_effects::detail;
    auto copy = std::make_unique<NativeEffectSchema>();
    alignas(8) std::byte instance[0x60]{}, user[0x50]{}, integer[104]{}, real[104]{};
    const void* definitions[]{integer, real};
    write<const void*>(instance, 0x58, user);
    write<const char*>(user, 0x10, "TestWeapon");
    write<std::uint16_t>(user, 0x44, 2);
    write<const void*>(user, 0x48, definitions);
    write<const char*>(integer, 8, "IntRange");
    write<unsigned>(integer, 0x58, 1);
    write<int>(integer, 96, -9);
    write<int>(integer, 100, 87);
    write<const char*>(real, 8, "FloatRange");
    write<unsigned>(real, 0x58, 2);
    write<float>(real, 96, -0.25f);
    write<float>(real, 100, 2.5f);
    REQUIRE(copySchema(*copy, instance));
    CHECK(read<int>(copy->definitions[0], 96) == -9);
    CHECK(read<int>(copy->definitions[0], 100) == 87);
    CHECK(read<float>(copy->definitions[1], 96) == -0.25f);
    CHECK(read<float>(copy->definitions[1], 100) == 2.5f);
}

TEST_CASE("equipment effects retain all slot bits across recorded frame copies") {
    PoseFrameHeader header{};
    header.bodyModelCount = 3;
    header.key = {99, 7, 4};
    setEquipmentEffectMask(header, (std::uint64_t{1} << 63) | 1);
    const auto recorded = header;
    setEquipmentEffectMask(header, 0);
    CHECK(equipmentEffectSelected(recorded, 0));
    CHECK(equipmentEffectSelected(recorded, 63));
    CHECK_FALSE(equipmentEffectSelected(recorded, 1));
    CHECK_FALSE(equipmentEffectSelected(recorded, 64));
    CHECK(recorded.bodyModelCount == 3);
    CHECK(recorded.key.generation == 7);
    CHECK(equipmentEffectMask(header) == 0);
}

TEST_CASE("temporary effect masks restore once and reject reused native identities") {
    EffectMaskRestoration<2> masks;
    EffectMaskIdentity first{11, 100, 7}, second{22, 200, 8};
    REQUIRE(masks.remember(first, 0x81234567));
    REQUIRE(masks.remember(second, 0));
    CHECK_FALSE(masks.remember({33, 300, 9}, 42));
    CHECK_FALSE(masks.remember(first, 0)); // Cannot overwrite the saved mask.
    CHECK(masks.restore(first) == 0x81234567);
    CHECK_FALSE(masks.restore(first));
    CHECK(masks.restore(second) == 0); // Zero is a valid original mask.
    REQUIRE(masks.remember(first, 0x1234));
    CHECK_FALSE(masks.restore({11, 100, 8})); // Same allocation, new emitter ID.
    REQUIRE(masks.remember(first, 0x4321));
    CHECK_FALSE(masks.restore({11, 101, 7})); // Same executor, another emitter.
    CHECK_FALSE(masks.restore(first));
    REQUIRE(masks.remember(first, 0x99));
    CHECK_FALSE(masks.restore(second)); // Foreign cleanup cannot consume it.
    CHECK(masks.restore(first) == 0x99);
}
