// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace bivouac::presentation {

struct CampStampRenderResult {
    bool ownedCampStamp = false;
    bool frameSelected = false;
    bool scaleAdjusted = false;
    // Diagnostics: the marker scale vanilla wrote this frame and the inputs of our formula.
    bool markerFound = false;
    bool fixedScale = false;
    float zoom = 0.0f;
    float vanillaMarkerScale = 0.0f;
    float writtenScale = 0.0f;
};

// The widget binds a GameData slot handle; this is its index field (+8).
std::uint32_t campStampWidgetHandleIndex(std::uintptr_t widgetContext);

// Rewrites a stamp widget's cached map cell (+0x68/+0x6C): 1 rewritten, 0 current, -1 not found.
int refreshCampStampCell(
    std::uintptr_t iconScreen, std::uint32_t stampSlot, std::uint32_t handleIndex,
    float mapX, float mapZ);

// Forces the next minimap icon pass to re-filter every icon.
void forceMinimapRefilter(std::uintptr_t iconScreen);

CampStampRenderResult adjustCampStampWidget(
    std::uintptr_t widgetContext,
    std::uintptr_t mainBase,
    bool fullScreenMapOpen,
    bool nearLink);

} // namespace bivouac::presentation
