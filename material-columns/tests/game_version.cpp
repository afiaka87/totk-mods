// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/native/GameVersion.hpp"
#include <array>
#include <cassert>
using namespace material_columns::native;
struct Site { std::uintptr_t offset; std::uint32_t first, second; };
int main() {
    alignas(8) static std::array<std::uintptr_t, 0x950000> memory{};
    const auto base = reinterpret_cast<std::uintptr_t>(memory.data());
    auto set = [&](std::uintptr_t offset, std::uintptr_t value) { memory[offset / 8] = value; };
#ifdef MATERIAL_COLUMNS_TARGET_142
    assert(nativeFrameOffset() == 16);
    assert(nativeOffset(0x1a584ec) == 0x20edeac);
    assert(nativeOffset(0x2a66f00) == 0x86ea4c);
    assert(nativeOffset(0x123a220) == 0xc73118);
    assert(nativeOffset(0xabcdef) == 0);
    const auto site = nativeSite(Site{0x1a584ec, 0, 0});
    assert(site.offset == 0x20edeac && site.first == 0xa9be7bfd);
    set(0x3ac46b0, 123);
    set(0x3ac8840, 456);
    set(0x3acb318, 789);
    set(0x394e1e8, base + 16);
#else
    assert(nativeFrameOffset() == 8);
    assert(nativeOffset(0x1a584ec) == 0x1a584ec);
    const auto site = nativeSite(Site{0x1a584ec, 1, 2});
    assert(site.offset == 0x1a584ec && site.first == 1 && site.second == 2);
    set(0x462e3d8, base + 24);
    set(0x462df08, base + 32);
    set(0x4728620, 789);
    set(0x4630020, base + 16);
    set(24, 123); set(32, 456);
#endif
    set(16, 321);
    assert(nativeSingleton(base, 0x462e3d8) == 123);
    assert(nativeSingleton(base, 0x462df08) == 456);
    assert(nativeSingleton(base, 0x4728620) == 789);
    assert(nativeSingleton(base, 0x4630020) == 321);
}
