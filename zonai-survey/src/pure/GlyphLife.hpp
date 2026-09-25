// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cstdint>

#include "GlyphTables.hpp"
#include "IconGlyphs.hpp"

namespace zonai_survey::pure {

inline constexpr std::uint32_t kGlyphHoldTicks = 1200;
inline constexpr std::uint32_t kGlyphFadeTicks = 600;
inline constexpr std::uint32_t kGlyphLifeTicks = kGlyphHoldTicks + kGlyphFadeTicks;

inline constexpr std::uint32_t kMaxGlyphs = 48;

inline float glyphAlpha(std::uint32_t age) {
    if (age < kGlyphHoldTicks) return 1.0f;
    if (age >= kGlyphLifeTicks) return 0.0f;
    const float into = static_cast<float>(age - kGlyphHoldTicks);
    return 1.0f - into / static_cast<float>(kGlyphFadeTicks);
}

inline bool glyphDead(std::uint32_t age) { return age >= kGlyphLifeTicks; }

struct Glyph {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float distanceSq = 0.0f;
    std::uint32_t age = 0;
    std::uint16_t name = 0;
    std::uint16_t flags = 0;
    std::uint8_t cls = 0;
    std::uint8_t icon = 0;
};

inline float selectionKeyOf(float distanceSq) { return distanceSq; }

inline bool withinCone(float dx, float dz, float headingX, float headingZ, float cosHalfAngle) {
    const float lengthSq = dx * dx + dz * dz;
    if (!(lengthSq > 0.0001f)) return true;
    const float projection = dx * headingX + dz * headingZ;
    if (projection <= 0.0f) return false;
    return projection * projection >= cosHalfAngle * cosHalfAngle * lengthSq;
}

inline const char* glyphIcon(std::uint8_t icon) { return icons::iconUtf8(icon); }

struct Tint {
    float r, g, b;
};

inline constexpr Tint kTintGrowing{0.62f, 0.93f, 0.55f};
inline constexpr Tint kTintCreature{0.98f, 0.84f, 0.58f};
inline constexpr Tint kTintHostile{1.00f, 0.45f, 0.40f};
inline constexpr Tint kTintMineral{0.72f, 0.78f, 1.00f};
inline constexpr Tint kTintTreasure{1.00f, 0.86f, 0.35f};
inline constexpr Tint kTintGear{0.85f, 0.90f, 0.97f};
inline constexpr Tint kTintFire{1.00f, 0.62f, 0.28f};
inline constexpr Tint kTintShock{1.00f, 0.94f, 0.45f};
inline constexpr Tint kTintIce{0.62f, 0.92f, 1.00f};
inline constexpr Tint kTintSplash{0.55f, 0.75f, 1.00f};
inline constexpr Tint kTintGlow{0.78f, 1.00f, 0.86f};
inline constexpr Tint kTintZonai{0.55f, 0.95f, 0.90f};

inline Tint glyphTint(std::uint8_t icon) {
    switch (static_cast<icons::Icon>(icon)) {
        case icons::Icon::Fire:        return kTintFire;
        case icons::Icon::Shock:       return kTintShock;
        case icons::Icon::Ice:         return kTintIce;
        case icons::Icon::Splash:      return kTintSplash;
        case icons::Icon::Glow:        return kTintGlow;
        case icons::Icon::Zonai:       return kTintZonai;

        case icons::Icon::Material:
        case icons::Icon::Mushroom:
        case icons::Icon::Plant:
        case icons::Icon::Wood:
        case icons::Icon::Stamina:     return kTintGrowing;

        case icons::Icon::Fish:
        case icons::Icon::Critter:
        case icons::Icon::Meat:
        case icons::Icon::Food:
        case icons::Icon::Heart:       return kTintCreature;

        case icons::Icon::Enemy:
        case icons::Icon::MonsterPart: return kTintHostile;

        case icons::Icon::Ore:         return kTintMineral;

        case icons::Icon::Chest:
        case icons::Icon::Rupee:
        case icons::Icon::KeyItem:     return kTintTreasure;

        case icons::Icon::Sword:
        case icons::Icon::BigSword:
        case icons::Icon::Spear:
        case icons::Icon::Shield:
        case icons::Icon::Bow:
        case icons::Icon::Arrow:
        case icons::Icon::Armor:       return kTintGear;

        case icons::Icon::Count:       break;
    }
    return kTintGrowing;
}

class GlyphPicker {
  public:
    void reset() {
        count_ = 0;
        overflow_ = 0;
        worst_ = 0;
    }

    void offer(const Glyph& candidate) {
        if (count_ < kMaxGlyphs) {
            slots_[count_++] = candidate;
            if (count_ == kMaxGlyphs) refreshWorst();
            return;
        }
        ++overflow_;
        if (candidate.distanceSq >= slots_[worst_].distanceSq) return;
        slots_[worst_] = candidate;
        refreshWorst();
    }

    const Glyph* begin() const { return slots_; }
    const Glyph* end() const { return slots_ + count_; }
    std::uint32_t count() const { return count_; }
    std::uint32_t overflow() const { return overflow_; }
    const Glyph& operator[](std::uint32_t i) const { return slots_[i]; }

  private:
    void refreshWorst() {
        worst_ = 0;
        for (std::uint32_t i = 1; i < count_; ++i) {
            if (slots_[i].distanceSq > slots_[worst_].distanceSq) worst_ = i;
        }
    }

    Glyph slots_[kMaxGlyphs]{};
    std::uint32_t count_ = 0;
    std::uint32_t overflow_ = 0;
    std::uint32_t worst_ = 0;
};

}
