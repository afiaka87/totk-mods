// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "State.hpp"

namespace botw_abilities::champions {

struct VowFlags {
    std::uint32_t original;
    std::uint32_t upgraded;
};

// IsGet.Obj_SageSoul[Plus]_* in the clean 1.2.1 GameData schema.
inline constexpr std::array<VowFlags, kBlessingCount> kVowFlags{{
    {0xE39184A3, 0x01F5D8AE},
    {0x3D8E285B, 0x82507BBD},
    {0x0695F2C9, 0x20C68248},
    {0x9A01509D, 0x83F90D87},
}};

template <typename ReadBool>
bool hasSageVow(Blessing blessing, ReadBool read) {
    const auto flags = kVowFlags[static_cast<std::size_t>(blessing)];
    return read(flags.original) || read(flags.upgraded);
}

} // namespace botw_abilities::champions
