// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
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
} // namespace botw_abilities::wheel
