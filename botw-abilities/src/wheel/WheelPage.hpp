// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstddef>
#include <cstdint>

namespace botw_abilities::wheel {
// Marks an ability as picked from the abilities page on a wheel screen, before the wheel exits.
using Choose = bool (*)(std::uintptr_t screen);
// Replaces an offered wedge's caption while it is highlighted; null keeps the wedge's own name.
using Caption = const char16_t *(*)();
using Available = bool (*)();
// Adds the abilities page: hold L, tap R to turn the page. A null Choose leaves that wedge empty.
// Install after the abilities so these wheel hooks run first. False when the game does not match.
bool install(std::uintptr_t base, Choose stasis, Choose cryonis, Choose sphere, Choose cube, Choose wolf = nullptr,
             Caption wolfCaption = nullptr, Available wolfAvailable = nullptr);

// Zonai Fieldworks' switch page: wedges clockwise from the top; exit cancels; release on a valid wedge chooses.
struct ExtraPage {
    bool (*abilitiesShown)();               // false hides the abilities page for this opening
    const char *(*icon)(std::size_t wedge);  // null leaves the wedge empty
    bool (*valid)(std::size_t wedge);
    bool (*checked)(std::size_t wedge);
    const char16_t *(*caption)(std::size_t wedge);
    void (*choose)(std::size_t wedge);
};
void setExtraPage(const ExtraPage *page);
} // namespace botw_abilities::wheel
