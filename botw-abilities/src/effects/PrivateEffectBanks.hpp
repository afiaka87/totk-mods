// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
#include <string_view>

namespace botw_abilities::effects {
constexpr const char* privateBank(std::string_view user) {
    if (user == "EftLoc_BotwDarukImpact") return "Sage_Soul_Goron";
    if (user == "EftLoc_BotwUrbosaRange") return "Sage_Soul_Gerudo";
    if (user == "TotkNativeSphereExplosion") return "TotkNativeSphere";
    if (user == "TotkRemoteBomb") return "SpObj_TimerBomb_A_01";
    return nullptr;
}

constexpr const char* missingPrivateBank(std::uintptr_t dictionary, std::uintptr_t bankDictionary,
                                        std::string_view user) {
    return bankDictionary && dictionary == bankDictionary ? privateBank(user) : nullptr;
}
}
