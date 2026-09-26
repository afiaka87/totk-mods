// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace zonai_hookshot::input_policy {
// Nintendo Npad L and ZL.
inline constexpr std::uint64_t kButtonL=1ull<<6;
inline constexpr std::uint64_t kButtonZL=1ull<<8;
inline constexpr std::uint64_t kAimChord=kButtonZL|kButtonL;
// The chord must not include the stick clicks.
static_assert((kAimChord & ((1ull<<4)|(1ull<<5))) == 0);
inline constexpr bool aimHeld(std::uint64_t buttons) {
    return (buttons&kAimChord)==kAimChord;
}
}
