// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "CampStampRenderer.hpp"

namespace bivouac::presentation {
namespace {

constexpr std::ptrdiff_t kGameDataManagerIndirectOffset = 0x0462E3D8;
constexpr std::ptrdiff_t kGetStructEnumOffset = 0x00BAD788;

constexpr std::uint32_t kIconTypeFieldHash = 1952810469;
constexpr std::uint32_t kCampStampIcon = 184548161;

constexpr std::uintptr_t kWidgetAnimationOffset = 80;
constexpr std::uintptr_t kWidgetGameDataHandleOffset = 88;
constexpr std::uintptr_t kHandleIndexOffset = 8;
constexpr std::uintptr_t kAnimationCurrentFrameOffset = 32;
constexpr std::uintptr_t kAnimationSetFrameVtableOffset = 208;
constexpr float kCampStampFrame = 10.0f;

constexpr std::uintptr_t kWidgetMarkerLinkOffset = 32;
constexpr std::uintptr_t kMarkerHolderMarkerOffset = 24;
constexpr std::uintptr_t kMarkerScaleXOffset = 72;
constexpr std::uintptr_t kMarkerScaleYOffset = 76;
constexpr std::uintptr_t kWidgetFixedScaleOffset = 57;
constexpr std::uintptr_t kWidgetSharedStateOffset = 48;
constexpr std::uintptr_t kSharedStateZoomPointerOffset = 16;
constexpr std::uintptr_t kScreenStampWidgetCountOffset = 0x4D8;
constexpr std::uintptr_t kScreenStampWidgetArrayOffset = 0x4E0;
constexpr std::uintptr_t kWidgetMapCellOffset = 0x68;
constexpr std::uintptr_t kScreenLinkCellOffset = 0x1030;
constexpr float kMapScale = 0.70f;
constexpr float kMinimapScale = 0.0f;

bool validPointer(std::uintptr_t value) {
    return value >= 0x1000000 && value < 0x8000000000ull
        && (value & 0x3) == 0;
}

} // namespace

std::uint32_t campStampWidgetHandleIndex(std::uintptr_t widgetContext) {
    if (!validPointer(widgetContext)) {
        return 0xFFFFFFFFu;
    }
    return *reinterpret_cast<std::uint32_t*>(
        widgetContext + kWidgetGameDataHandleOffset + kHandleIndexOffset);
}

int refreshCampStampCell(
    std::uintptr_t iconScreen, std::uint32_t stampSlot, std::uint32_t handleIndex,
    float mapX, float mapZ) {
    if (!validPointer(iconScreen)) {
        return -1;
    }
    const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(
        iconScreen + kScreenStampWidgetCountOffset);
    const auto widgets = *reinterpret_cast<std::uintptr_t*>(
        iconScreen + kScreenStampWidgetArrayOffset);
    if (stampSlot >= count || !validPointer(widgets)) {
        return -1;
    }
    const auto widget = reinterpret_cast<std::uintptr_t*>(widgets)[stampSlot];
    if (!validPointer(widget) || campStampWidgetHandleIndex(widget) != handleIndex) {
        return -1;
    }
    // Same arithmetic as the game's own pass: truncate to int, then divide by 10.
    const std::int32_t cellX = static_cast<std::int32_t>(mapX) / 10;
    const std::int32_t cellY = static_cast<std::int32_t>(-mapZ) / 10;
    auto* cell = reinterpret_cast<std::int32_t*>(widget + kWidgetMapCellOffset);
    if (cell[0] == cellX && cell[1] == cellY) {
        return 0;
    }
    cell[0] = cellX;
    cell[1] = cellY;
    return 1;
}

void forceMinimapRefilter(std::uintptr_t iconScreen) {
    if (validPointer(iconScreen)) {
        *reinterpret_cast<std::int32_t*>(iconScreen + kScreenLinkCellOffset) = 0x7FFFFFFF;
    }
}

CampStampRenderResult adjustCampStampWidget(
    std::uintptr_t widgetContext, std::uintptr_t mainBase,
    bool fullScreenMapOpen, bool nearLink) {
    CampStampRenderResult result{};
    if (!validPointer(widgetContext)) {
        return result;
    }

    const auto managerIndirect = *reinterpret_cast<std::uintptr_t*>(
        mainBase + kGameDataManagerIndirectOffset);
    if (!validPointer(managerIndirect)) {
        return result;
    }
    const auto manager = *reinterpret_cast<std::uintptr_t*>(managerIndirect);
    if (!validPointer(manager)) {
        return result;
    }

    const void* handle = reinterpret_cast<const void*>(
        widgetContext + kWidgetGameDataHandleOffset);
    std::uint32_t iconType = 0;
    const auto getEnum =
        reinterpret_cast<std::uint32_t (*)(std::uintptr_t, std::uint32_t*,
                                           const void*, std::uint32_t)>(
            mainBase + kGetStructEnumOffset);
    if ((getEnum(manager, &iconType, handle, kIconTypeFieldHash) & 1u) == 0
        || iconType != kCampStampIcon) {
        return result;
    }
    result.ownedCampStamp = true;

    const auto animation =
        *reinterpret_cast<std::uintptr_t*>(
            widgetContext + kWidgetAnimationOffset);
    if (validPointer(animation)
        && *reinterpret_cast<float*>(
               animation + kAnimationCurrentFrameOffset) != kCampStampFrame) {
        const auto vtable = *reinterpret_cast<std::uintptr_t*>(animation);
        if (validPointer(vtable)) {
            const auto setFrame = *reinterpret_cast<std::uintptr_t*>(
                vtable + kAnimationSetFrameVtableOffset);
            if (validPointer(setFrame)) {
                reinterpret_cast<void (*)(std::uintptr_t, float)>(
                    setFrame)(animation, kCampStampFrame);
                result.frameSelected = true;
            }
        }
    }

    const auto markerHolder = *reinterpret_cast<std::uintptr_t*>(
        widgetContext + kWidgetMarkerLinkOffset);
    if (!validPointer(markerHolder)) {
        return result;
    }
    const auto marker = *reinterpret_cast<std::uintptr_t*>(
        markerHolder + kMarkerHolderMarkerOffset);
    if (!validPointer(marker)) {
        return result;
    }

    result.markerFound = true;
    result.vanillaMarkerScale =
        *reinterpret_cast<float*>(marker + kMarkerScaleXOffset);
    float vanillaScale = 1.0f;
    result.fixedScale = *reinterpret_cast<std::uint8_t*>(
        widgetContext + kWidgetFixedScaleOffset) != 0;
    if (!result.fixedScale) {
        const auto sharedState = *reinterpret_cast<std::uintptr_t*>(
            widgetContext + kWidgetSharedStateOffset);
        if (!validPointer(sharedState)) {
            return result;
        }
        const auto zoomPointer = *reinterpret_cast<std::uintptr_t*>(
            sharedState + kSharedStateZoomPointerOffset);
        if (!validPointer(zoomPointer)) {
            return result;
        }
        const float zoom = *reinterpret_cast<float*>(zoomPointer);
        result.zoom = zoom;
        if (zoom > 0.0f) {
            vanillaScale = 1.0f / zoom;
        }
    }

    // Near camp on the minimap: keep vanilla's scale unless it is still our earlier zero.
    if (nearLink && !fullScreenMapOpen) {
        if (result.vanillaMarkerScale > 0.001f) {
            result.writtenScale = result.vanillaMarkerScale;
            return result;
        }
        result.writtenScale = vanillaScale;
        *reinterpret_cast<float*>(marker + kMarkerScaleXOffset) = result.writtenScale;
        *reinterpret_cast<float*>(marker + kMarkerScaleYOffset) = result.writtenScale;
        result.scaleAdjusted = true;
        return result;
    }

    const float ownedScale = fullScreenMapOpen ? kMapScale : kMinimapScale;
    result.writtenScale = vanillaScale * ownedScale;
    *reinterpret_cast<float*>(marker + kMarkerScaleXOffset) = result.writtenScale;
    *reinterpret_cast<float*>(marker + kMarkerScaleYOffset) = result.writtenScale;
    result.scaleAdjusted = true;
    return result;
}

} // namespace bivouac::presentation
