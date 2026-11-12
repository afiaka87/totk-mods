// SPDX-License-Identifier: MIT
// Copyright (C) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>
#if defined(TOTK_PORT) && TOTK_PORT
#include "totk/port/Port.hpp"
#endif
namespace totk::ui::native_font::offsets {
// With TOTK_PORT each place is named by its 1.2.1 offset and translated to the running game at use.
#if defined(TOTK_PORT) && TOTK_PORT
using Place = totk::port::Code;
#else
using Place = std::uintptr_t;
#endif
// TotK 1.2.1; unchanged accepted BindFS native UI/render ABI, donor source 699142ef.
constexpr Place graphics = 0x4716e48, getProc = 0x46707e8;
constexpr Place contextCtor = 0x74c19c, contextApply = 0x756a08, barrier = 0xc4b7ac;
constexpr Place uiDraw = 0xb0f844, viewportApply = 0x74f09c;
constexpr Place viewportSize = 0xe8413c, renderBufferBind = 0x7b9274;
} // namespace totk::ui::native_font::offsets
