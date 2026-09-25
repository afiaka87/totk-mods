// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cstdint>

#include "GlyphTables.hpp"
#include "PackedGlyphs.hpp"
#ifndef SURVEY_COMPACT_MAP
#define SURVEY_COMPACT_MAP 0
#endif

namespace zonai_survey::pure {

inline std::uint32_t actorHash(const char* name) {
    std::uint32_t h = 0x811C9DC5u;
    if (!name) return h;
    for (const char* p = name; *p; ++p) {
        h ^= static_cast<std::uint8_t>(*p);
        h *= 0x01000193u;
    }
    return h;
}

inline constexpr std::uint32_t kNoGlyphName = 0xFFFFFFFFu;

inline std::uint32_t findGlyphName(const char* actorName) {
    if (!actorName || !*actorName) return kNoGlyphName;
    const std::uint32_t want = actorHash(actorName);
    std::uint32_t lo = 0;
    std::uint32_t hi = glyphs::kNameCount;
    while (lo < hi) {
        const std::uint32_t mid = lo + (hi - lo) / 2;
        const std::uint32_t got = glyphs::kNames[mid].hash;
        if (got == want) return mid;
        if (got < want) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return kNoGlyphName;
}

inline const char* glyphDisplayName(std::uint32_t nameIndex) {
    if (nameIndex >= glyphs::kNameCount) return nullptr;
    const std::uint16_t offset = glyphs::kNames[nameIndex].textOffset;
    if (offset == glyphs::kNoName) return nullptr;
    return glyphs::kNameBlob + offset;
}

inline glyphs::GlyphClass glyphClassOf(std::uint32_t nameIndex) {
    if (nameIndex >= glyphs::kNameCount) return glyphs::GlyphClass::Material;
    return static_cast<glyphs::GlyphClass>(glyphs::kNames[nameIndex].cls);
}

inline std::uint8_t glyphIconOf(std::uint32_t nameIndex) {
    if (nameIndex >= glyphs::kNameCount) return 0;
    return glyphs::kNames[nameIndex].icon;
}

inline int floorDiv(float coord) {
    const float scaled = coord / glyphs::kCellSize;
    const int truncated = static_cast<int>(scaled);
    return (scaled < 0.0f && static_cast<float>(truncated) != scaled) ? truncated - 1 : truncated;
}

inline float placementWorldX(const glyphs::Placement& p) {
    return static_cast<float>(p.x) * glyphs::kPosScale;
}
inline float placementWorldY(const glyphs::Placement& p) {
    return static_cast<float>(p.y) * glyphs::kPosScale;
}
inline float placementWorldZ(const glyphs::Placement& p) {
    return static_cast<float>(p.z) * glyphs::kPosScale;
}

template <bool Packed = (SURVEY_COMPACT_MAP != 0), class Visit>
inline void forEachPlacementNear(float centreX, float centreZ, float radius, Visit&& visit) {
    if (!(radius > 0.0f)) return;

    const int minCellX = floorDiv(centreX - radius) - glyphs::kGridOriginCellX;
    const int maxCellX = floorDiv(centreX + radius) - glyphs::kGridOriginCellX;
    const int minCellZ = floorDiv(centreZ - radius) - glyphs::kGridOriginCellZ;
    const int maxCellZ = floorDiv(centreZ + radius) - glyphs::kGridOriginCellZ;

    const int loX = minCellX < 0 ? 0 : minCellX;
    const int hiX = maxCellX >= glyphs::kGridWidth ? glyphs::kGridWidth - 1 : maxCellX;
    const int loZ = minCellZ < 0 ? 0 : minCellZ;
    const int hiZ = maxCellZ >= glyphs::kGridHeight ? glyphs::kGridHeight - 1 : maxCellZ;

    const float radiusSq = radius * radius;
    for (int cz = loZ; cz <= hiZ; ++cz) {
        for (int cx = loX; cx <= hiX; ++cx) {
            const std::uint32_t cell = static_cast<std::uint32_t>(cz) * glyphs::kGridWidth +
                                       static_cast<std::uint32_t>(cx);
            const std::uint32_t begin = glyphs::kCellStart[cell];
            const std::uint32_t end = glyphs::kCellStart[cell + 1];
            for (std::uint32_t i = begin; i < end; ++i) {
                const auto& p = [&]() -> decltype(auto) {
                    if constexpr (Packed) return glyphs::unpackPlacement(i,cell);
                    else return (glyphs::kPlacements[i]);
                }();
                const float dx = placementWorldX(p) - centreX;
                const float dz = placementWorldZ(p) - centreZ;
                const float d2 = dx * dx + dz * dz;
                if (d2 > radiusSq) continue;
                visit(p, d2);
            }
        }
    }
}

}
