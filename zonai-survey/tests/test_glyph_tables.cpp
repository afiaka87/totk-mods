// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#include <cmath>
#include <cstring>

#include "doctest.h"

#include "GlyphIndex.hpp"
#include "PulseLattice.hpp"
#include "GlyphLife.hpp"
#include "GlyphTables.hpp"
#include "IconGlyphs.hpp"

using namespace zonai_survey;

namespace {

bool fontHasGlyph(std::uint32_t codePoint) {
    if (codePoint > 0xFFFFu) return false;
    std::uint32_t lo = 0;
    std::uint32_t hi = glyphs::kFontGlyphCount;
    while (lo < hi) {
        const std::uint32_t mid = lo + (hi - lo) / 2;
        const std::uint32_t got = glyphs::kFontGlyphs[mid];
        if (got == codePoint) return true;
        if (got < codePoint) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return false;
}

struct Decoded {
    std::uint32_t codePoint = 0;
    int length = 0;
};

Decoded decodeUtf8(const char* s) {
    const auto b0 = static_cast<std::uint8_t>(s[0]);
    if (b0 == 0) return {};
    if ((b0 & 0x80u) == 0) return {b0, 1};
    if ((b0 & 0xE0u) == 0xC0u) {
        const auto b1 = static_cast<std::uint8_t>(s[1]);
        if ((b1 & 0xC0u) != 0x80u) return {};
        return {((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu), 2};
    }
    if ((b0 & 0xF0u) == 0xE0u) {
        const auto b1 = static_cast<std::uint8_t>(s[1]);
        const auto b2 = static_cast<std::uint8_t>(s[2]);
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) return {};
        return {((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu), 3};
    }
    return {};
}

constexpr glyphs::GlyphClass kAllClasses[] = {
    glyphs::GlyphClass::Material,     glyphs::GlyphClass::Food,
    glyphs::GlyphClass::Weapon,       glyphs::GlyphClass::Shield,
    glyphs::GlyphClass::Bow,          glyphs::GlyphClass::Arrow,
    glyphs::GlyphClass::KeyItem,      glyphs::GlyphClass::SpecialParts,
    glyphs::GlyphClass::Rupee,        glyphs::GlyphClass::Armor,
    glyphs::GlyphClass::SpecialPower, glyphs::GlyphClass::Ore,
    glyphs::GlyphClass::Chest,        glyphs::GlyphClass::Enemy,
};

}

TEST_CASE("every icon is a cell the shipped font can actually draw") {

    for (std::uint8_t i = 0; i < icons::kIconCount; ++i) {
        const char* icon = icons::kIconUtf8[i];
        REQUIRE(icon != nullptr);

        const Decoded decoded = decodeUtf8(icon);

        CHECK_MESSAGE(decoded.length > 0, "icon is not valid 1-3 byte UTF-8");
        CHECK(std::strlen(icon) == static_cast<std::size_t>(decoded.length));
        CHECK_MESSAGE(fontHasGlyph(decoded.codePoint),
                      "icon has no cell in nvn_font_jis1_tbl.bin");

        CHECK_MESSAGE(decoded.codePoint >= 0x7F, "icon sits in an ASCII cell");
    }
}

TEST_CASE("every icon has its own cell") {
    for (std::uint8_t i = 0; i < icons::kIconCount; ++i) {
        for (std::uint8_t j = static_cast<std::uint8_t>(i + 1); j < icons::kIconCount; ++j) {
            CHECK_MESSAGE(std::strcmp(icons::kIconUtf8[i], icons::kIconUtf8[j]) != 0,
                          "two icons share a code point, so one silently overwrote the other");
        }
    }
}

TEST_CASE("every named collectible carries an icon the table defines") {
    for (std::uint32_t i = 0; i < glyphs::kNameCount; ++i) {
        CHECK(glyphs::kNames[i].icon < icons::kIconCount);
    }
}

TEST_CASE("every icon has a colour, and the colours are in range") {
    for (std::uint8_t i = 0; i < icons::kIconCount; ++i) {
        const pure::Tint tint = pure::glyphTint(i);
        CHECK(tint.r >= 0.0f);
        CHECK(tint.r <= 1.0f);
        CHECK(tint.g >= 0.0f);
        CHECK(tint.g <= 1.0f);
        CHECK(tint.b >= 0.0f);
        CHECK(tint.b <= 1.0f);

        CHECK(tint.r + tint.g + tint.b > 0.9f);
    }
}

TEST_CASE("the icons that carry the world are the ones the rules promise") {

    struct Expected {
        const char* actor;
        icons::Icon icon;
    };
    const Expected kCases[] = {
        {"LightBall_Small", icons::Icon::Glow},
        {"FireFruit", icons::Icon::Fire},
        {"IceFruit", icons::Icon::Ice},
        {"WaterFruit", icons::Icon::Splash},
        {"ElectricalFruit", icons::Icon::Shock},
        {"Item_Mushroom_A", icons::Icon::Stamina},
        {"Item_Mushroom_D", icons::Icon::Mushroom},
        {"Item_Plant_A", icons::Icon::Plant},
        {"Item_Fruit_A", icons::Icon::Material},
        {"Animal_Fish_A", icons::Icon::Fish},
        {"Animal_Insect_E", icons::Icon::Critter},
        {"Obj_Mineral_A_01", icons::Icon::Ore},
        {"Enemy_Bokoblin_Junior", icons::Icon::Enemy},
        {"Weapon_Sword_001", icons::Icon::Sword},
        {"Weapon_Lsword_001", icons::Icon::BigSword},
        {"Weapon_Spear_001", icons::Icon::Spear},
        {"Weapon_Shield_001", icons::Icon::Shield},
        {"Weapon_Bow_001", icons::Icon::Bow},
        {"FldObj_Pinecone_A_01", icons::Icon::Wood},

        {"Item_Enemy_130", icons::Icon::Zonai},
        {"Item_Material_04", icons::Icon::Material},
        {"Item_Meat_01", icons::Icon::Meat},
    };
    for (const auto& expected : kCases) {
        const std::uint32_t index = pure::findGlyphName(expected.actor);
        REQUIRE_MESSAGE(index != pure::kNoGlyphName, expected.actor);
        CHECK_MESSAGE(pure::glyphIconOf(index) == static_cast<std::uint8_t>(expected.icon),
                      expected.actor);
    }
}

TEST_CASE("the name table is sorted, which is what makes the lookup a search") {
    for (std::uint32_t i = 1; i < glyphs::kNameCount; ++i) {
        CHECK(glyphs::kNames[i - 1].hash < glyphs::kNames[i].hash);
    }
}

TEST_CASE("known collectibles resolve, and nonsense does not") {

    const char* const known[] = {
        "Item_Fruit_A",
        "Item_Mushroom_D",
        "Animal_Fish_A",
        "Enemy_Bokoblin_Junior",
        "TBox_Field_Iron",
        "Obj_Mineral_A_01",
    };
    for (const char* name : known) {
        const std::uint32_t index = pure::findGlyphName(name);
        REQUIRE_MESSAGE(index != pure::kNoGlyphName, name);
        CHECK(pure::glyphDisplayName(index) != nullptr);
    }

    CHECK(pure::findGlyphName("Obj_Ore_A") == pure::kNoGlyphName);
    CHECK(pure::findGlyphName("Obj_TreasureBox") == pure::kNoGlyphName);
    CHECK(pure::findGlyphName("TwnObj_HatenoVillage_Fence_A_01") == pure::kNoGlyphName);
    CHECK(pure::findGlyphName("") == pure::kNoGlyphName);
    CHECK(pure::findGlyphName(nullptr) == pure::kNoGlyphName);
}

TEST_CASE("grass and trees exclude themselves, with no deny-list") {

    const char* const scenery[] = {
        "Obj_TreeApple_A_01", "FldObj_Tree_A_01",   "Obj_Grass_A_01",
        "Obj_BoxIron_A_01",   "FldObj_RockBig_A_01",
    };
    for (const char* name : scenery) {
        CHECK_MESSAGE(pure::findGlyphName(name) == pure::kNoGlyphName, name);
    }
}

TEST_CASE("display names are clean text, never control codes") {

    std::uint32_t named = 0;
    for (std::uint32_t i = 0; i < glyphs::kNameCount; ++i) {
        const char* text = pure::glyphDisplayName(i);
        if (!text) continue;
        ++named;
        CHECK(text[0] != '\0');
        for (const char* p = text; *p; ++p) {
            CHECK(static_cast<std::uint8_t>(*p) >= 0x20);
        }
    }
    CHECK(named > 1500);
}

TEST_CASE("the grid directory covers the placement table exactly") {
    CHECK(glyphs::kCellStart[0] == 0);
    CHECK(glyphs::kCellStart[glyphs::kGridWidth * glyphs::kGridHeight] ==
          glyphs::kPlacementCount);
    for (int i = 1; i <= glyphs::kGridWidth * glyphs::kGridHeight; ++i) {
        CHECK(glyphs::kCellStart[i - 1] <= glyphs::kCellStart[i]);
    }
}

TEST_CASE("every placement is filed in the cell its own position computes") {

    for (int cz = 0; cz < glyphs::kGridHeight; ++cz) {
        for (int cx = 0; cx < glyphs::kGridWidth; ++cx) {
            const std::uint32_t cell =
                static_cast<std::uint32_t>(cz) * glyphs::kGridWidth + static_cast<std::uint32_t>(cx);
            for (std::uint32_t i = glyphs::kCellStart[cell]; i < glyphs::kCellStart[cell + 1]; ++i) {
                const glyphs::Placement& p = glyphs::kPlacements[i];
                const int gotX = pure::floorDiv(pure::placementWorldX(p)) - glyphs::kGridOriginCellX;
                const int gotZ = pure::floorDiv(pure::placementWorldZ(p)) - glyphs::kGridOriginCellZ;
                REQUIRE(gotX == cx);
                REQUIRE(gotZ == cz);
            }
        }
    }
}

TEST_CASE("every placement names a real table entry") {
    for (std::uint32_t i = 0; i < glyphs::kPlacementCount; ++i) {
        REQUIRE(glyphs::kPlacements[i].name < glyphs::kNameCount);
    }
}

TEST_CASE("a radius query finds a placement that is inside it, and skips one that is not") {
    const glyphs::Placement& target = glyphs::kPlacements[glyphs::kPlacementCount / 2];
    const float tx = pure::placementWorldX(target);
    const float tz = pure::placementWorldZ(target);

    bool found = false;
    std::uint32_t visited = 0;
    pure::forEachPlacementNear(tx, tz, 4.0f, [&](const glyphs::Placement& p, float d2) {
        ++visited;
        CHECK(d2 <= 4.0f * 4.0f);
        if (&p == &target) found = true;
    });
    CHECK(found);
    CHECK(visited > 0);

    std::uint32_t strays = 0;
    pure::forEachPlacementNear(-99000.0f, -99000.0f, 50.0f,
                               [&](const glyphs::Placement&, float) { ++strays; });
    CHECK(strays == 0);
}

TEST_CASE("a full-range query stays bounded and honours the radius") {
    std::uint32_t visited = 0;
    pure::forEachPlacementNear(0.0f, 0.0f, 443.0f, [&](const glyphs::Placement& p, float d2) {
        ++visited;
        const float dx = pure::placementWorldX(p);
        const float dz = pure::placementWorldZ(p);
        CHECK(d2 <= 443.0f * 443.0f + 1.0f);
        CHECK(dx * dx + dz * dz <= 443.0f * 443.0f + 1.0f);
    });

    CHECK(visited < glyphs::kPlacementCount / 4);
}

TEST_CASE("a glyph holds at full strength, then fades, then is gone") {
    CHECK(pure::glyphAlpha(0) == doctest::Approx(1.0f));
    CHECK(pure::glyphAlpha(pure::kGlyphHoldTicks - 1) == doctest::Approx(1.0f));

    float previous = 1.0f;
    for (std::uint32_t age = pure::kGlyphHoldTicks; age < pure::kGlyphLifeTicks; ++age) {
        const float alpha = pure::glyphAlpha(age);
        CHECK(alpha <= previous);
        CHECK(alpha >= 0.0f);
        previous = alpha;
    }

    CHECK(pure::glyphAlpha(pure::kGlyphLifeTicks) == doctest::Approx(0.0f));
    CHECK_FALSE(pure::glyphDead(pure::kGlyphLifeTicks - 1));
    CHECK(pure::glyphDead(pure::kGlyphLifeTicks));

    CHECK(pure::kGlyphLifeTicks == 1800);
}

TEST_CASE("the cap keeps the nearest 48 and reports what it turned away") {
    pure::GlyphPicker picker;
    picker.reset();

    for (std::uint32_t i = 0; i < 60; ++i) {
        pure::Glyph g{};
        g.distanceSq = static_cast<float>(60 - i);
        g.name = static_cast<std::uint16_t>(i);
        picker.offer(g);
    }

    CHECK(picker.count() == pure::kMaxGlyphs);
    CHECK(picker.overflow() == 60 - pure::kMaxGlyphs);

    float worst = 0.0f;
    for (const pure::Glyph& g : picker) {
        if (g.distanceSq > worst) worst = g.distanceSq;
    }

    CHECK(worst == doctest::Approx(static_cast<float>(pure::kMaxGlyphs)));
}

TEST_CASE("under the cap, nothing is dropped") {
    pure::GlyphPicker picker;
    picker.reset();
    for (std::uint32_t i = 0; i < 10; ++i) {
        pure::Glyph g{};
        g.distanceSq = static_cast<float>(i);
        picker.offer(g);
    }
    CHECK(picker.count() == 10);
    CHECK(picker.overflow() == 0);
}

TEST_CASE("the cone accepts what is ahead and rejects what is not") {
    const float cosHalf = std::cos(pure::kConeRadians * 0.5f);

    CHECK(pure::withinCone(0.0f, 10.0f, 0.0f, 1.0f, cosHalf));
    CHECK(pure::withinCone(0.0f, 400.0f, 0.0f, 1.0f, cosHalf));

    CHECK_FALSE(pure::withinCone(0.0f, -10.0f, 0.0f, 1.0f, cosHalf));
    CHECK_FALSE(pure::withinCone(10.0f, 0.0f, 0.0f, 1.0f, cosHalf));
    CHECK_FALSE(pure::withinCone(-10.0f, 0.0f, 0.0f, 1.0f, cosHalf));

    const float inner = (pure::kConeRadians * 0.5f) * 0.9f;
    const float outer = (pure::kConeRadians * 0.5f) * 1.1f;
    CHECK(pure::withinCone(std::sin(inner) * 100.0f, std::cos(inner) * 100.0f, 0.0f, 1.0f, cosHalf));
    CHECK_FALSE(
        pure::withinCone(std::sin(outer) * 100.0f, std::cos(outer) * 100.0f, 0.0f, 1.0f, cosHalf));

    CHECK(pure::withinCone(0.0f, 0.0f, 0.0f, 1.0f, cosHalf));
}

TEST_CASE("the cone turns with the heading and keeps its shape") {
    const float cosHalf = std::cos(pure::kConeRadians * 0.5f);

    CHECK(pure::withinCone(10.0f, 0.0f, 1.0f, 0.0f, cosHalf));
    CHECK_FALSE(pure::withinCone(0.0f, 10.0f, 1.0f, 0.0f, cosHalf));
}

TEST_CASE("ranking is distance and nothing else") {

    pure::GlyphPicker picker;
    picker.reset();

    pure::Glyph nearCommon{};
    nearCommon.distanceSq = 70.0f * 70.0f;
    nearCommon.cls = static_cast<std::uint8_t>(glyphs::GlyphClass::Material);
    nearCommon.name = 1;

    pure::Glyph farRare{};
    farRare.distanceSq = 270.0f * 270.0f;
    farRare.cls = static_cast<std::uint8_t>(glyphs::GlyphClass::KeyItem);
    farRare.name = 2;

    for (std::uint32_t i = 0; i < pure::kMaxGlyphs; ++i) {
        pure::Glyph filler{};
        filler.distanceSq = 100.0f * 100.0f + static_cast<float>(i);
        filler.name = 100;
        picker.offer(filler);
    }
    picker.offer(farRare);
    picker.offer(nearCommon);

    bool keptNear = false;
    bool keptFar = false;
    for (const pure::Glyph& g : picker) {
        if (g.name == 1) keptNear = true;
        if (g.name == 2) keptFar = true;
    }
    CHECK(keptNear);
    CHECK_FALSE(keptFar);
    CHECK(pure::selectionKeyOf(nearCommon.distanceSq) == doctest::Approx(nearCommon.distanceSq));
}
