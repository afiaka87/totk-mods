// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "NativeField.hpp"
#include "FuryField.hpp"
#include "totk/port/CodeCheck.hpp"
#include <array>
#include <lib.hpp>

namespace botw_abilities::champions::field {
namespace {
using namespace totk::engine;
using namespace totk::port::literals;
constexpr totk::port::Site kSites[]{
    {0x1066DA0,0xF9411408,0xB4000068}, {0xD179E8,0xAA1F03E3,0x17F3A11E},
    {0x1DAF314,0xA9BD7BFD,0xF9000BF5},
    {0xAF78BC,0xA9BB7BFD,0xF9000BF9},
};
// The handle-validity routine: 1.4.3 has no such routine, so it is checked and called on 1.2.1 only.
constexpr totk::port::Site kSites121[]{{0xD17C80,0x79800408,0xB100051F}};
struct Handle {
    std::int8_t bank{};
    std::uint8_t padding{};
    std::int16_t index = -1;
    std::uint32_t serial{};
};
static_assert(sizeof(Handle) == 8);
std::uintptr_t g_base{};
Handle g_handle{};
std::uint32_t g_owner{};
float g_elapsed{};
template<class Fn> Fn call(totk::port::Code offset) { return reinterpret_cast<Fn>(g_base+offset); }

// TotK 1.4.3 only: the game does this slot-table lookup in line where 1.2.1 calls 0xD17C80.
bool live143(const Handle& handle) {
    if (handle.index == -1 || handle.bank < 0 || handle.bank > 1) return false;
    const auto tables = readMemory<std::uintptr_t>(g_base + 0x395F388_v143);
    const auto strides = readMemory<std::uintptr_t>(g_base + 0x395F390_v143);
    if (!isPlausibleAddress(tables) || !isPlausibleAddress(strides)) return false;
    const auto table = readMemory<std::uint64_t>(tables + std::uintptr_t(handle.bank) * 8);
    const auto stride = readMemory<std::uint64_t>(strides + std::uintptr_t(handle.bank) * 8);
    const std::uint64_t slot = stride * std::uint64_t(std::int64_t(handle.index)) + table;
    return isPlausibleAddress(slot) && readMemory<std::uint32_t>(slot + 0x20) == handle.serial;
}
bool live(const Handle& handle) {
    return totk::port::is143() ? live143(handle) : call<bool(*)(const Handle*)>(0xD17C80)(&handle);
}
}

bool install(std::uintptr_t base) {
    if (!totk::port::codeMatches(base,kSites,"champions","FIELD_BUILD_REFUSED")) return false;
    if (!totk::port::is143() && !totk::port::codeMatches(base,kSites121,"champions","FIELD_BUILD_REFUSED")) return false;
    g_base = base;
    return true;
}

void reset() {
    if (g_base && g_handle.index >= 0)
        call<void(*)(Handle*,unsigned)>(0xAF78BC)(&g_handle,6);
    g_handle = {};
    g_owner = 0;
    g_elapsed = 0;
}

void update(std::uintptr_t actor, const totk::core::WorldPosition& position, float seconds) {
    const auto id = readMemory<std::uint32_t>(actor+0x10);
    if (g_owner != id) { reset(); g_owner = id; }
    if (!live(g_handle)) {
        const auto component = call<std::uintptr_t(*)(std::uintptr_t)>(0x1066DA0)(actor);
        if (!isPlausibleAddress(component)) return;
        const auto instance = readMemory<std::uintptr_t>(component+0x70);
        if (!isPlausibleAddress(instance)) return;
        call<void(*)(std::uintptr_t,const char*,Handle*)>(0xD179E8)(instance,"FieldBarrier",&g_handle);
        if (!live(g_handle)) return;
        Logging.Log("[champions] FURY_FIELD_LIVE actor=%u handle=%d",id,int(g_handle.index));
    }
    g_elapsed = advanceFuryField(g_elapsed,seconds,true);
    const std::array<float,3> scale{g_elapsed,g_elapsed,g_elapsed};
    call<void(*)(Handle*,const totk::core::WorldPosition*,const float*)>(0x1DAF314)(
        &g_handle,&position,scale.data());
}
}
