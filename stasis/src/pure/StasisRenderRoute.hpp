// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
namespace stasis::render {
#if defined(STASIS_NATIVE_BLOOM) && STASIS_NATIVE_BLOOM
inline constexpr bool kNativeBloom = true;
#else
inline constexpr bool kNativeBloom = false;
#endif
// Values are shared with resources.w in stasis.frag.
enum class DrawPart { Complete, World, Hud };
struct DrawSchedule {
    bool beforeHud{}, afterWorld{};
};
// TotK 1.2.1 phase 1 precedes bloom; phase 3 follows it and releases the masks.
constexpr DrawSchedule nativeSchedule(unsigned phase, unsigned pass, unsigned view) {
    return pass == 0 && view == 0 ? DrawSchedule{phase == 3, phase == 1} : DrawSchedule{};
}
} // namespace stasis::render
