// SPDX-License-Identifier: MIT
// Copyright (C) Clay Mullis
#pragma once
#include <cstdint>
namespace totk::ui::native_font::offsets {
// TotK 1.2.1; unchanged accepted BindFS native UI/render ABI, donor source 699142ef.
constexpr std::uintptr_t graphics = 0x4716e48, getProc = 0x46707e8;
constexpr std::uintptr_t contextCtor = 0x74c19c, contextApply = 0x756a08, barrier = 0xc4b7ac;
constexpr std::uintptr_t uiDraw = 0xb0f844, viewportApply = 0x74f09c;
constexpr std::uintptr_t viewportSize = 0xe8413c, renderBufferBind = 0x7b9274;
} // namespace totk::ui::native_font::offsets
