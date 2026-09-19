// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace bivouac::map {

constexpr bool ownsStampType(std::uint32_t type,
                             std::uint32_t currentIcon,
                             std::uint32_t legacyIcon) {
    return type == currentIcon || type == legacyIcon;
}

// The camp whose owned stamp slot matches the selected slot, or -1.
template <typename Sites>
constexpr int siteOwningStampSlot(const Sites& sites, int siteCount,
                                  std::uint32_t slot) {
    for (int index = 0; index < siteCount; ++index) {
        if (sites[index].active
            && static_cast<std::uint32_t>(sites[index].stampSlot) == slot) {
            return index;
        }
    }
    return -1;
}

consteval bool contracts() {
    if (!ownsStampType(3, 3, 4)) return false;
    if (!ownsStampType(4, 3, 4)) return false;
    if (ownsStampType(5, 3, 4)) return false;
    struct Probe { bool active; std::uint16_t stampSlot; };
    const Probe probes[3] = {{true, 7}, {false, 9}, {true, 9}};
    if (siteOwningStampSlot(probes, 3, 7) != 0) return false;
    if (siteOwningStampSlot(probes, 3, 9) != 2) return false;
    if (siteOwningStampSlot(probes, 3, 8) != -1) return false;
    if (siteOwningStampSlot(probes, 3, 0xFFFFu) != -1) return false;
    return true;
}

static_assert(contracts(), "Bivouac map policy contracts must hold");

} // namespace bivouac::map
