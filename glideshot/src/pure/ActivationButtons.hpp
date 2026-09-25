// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace zonai_hookshot::input_policy {
// Nintendo Npad L and ZL, also used by the released v0.8.1 chord.
inline constexpr std::uint64_t kButtonL=1ull<<6;
inline constexpr std::uint64_t kButtonZL=1ull<<8;
inline constexpr std::uint64_t kAimChord=kButtonZL|kButtonL;
inline constexpr bool aimHeld(std::uint64_t buttons) {
    return (buttons&kAimChord)==kAimChord;
}
}
