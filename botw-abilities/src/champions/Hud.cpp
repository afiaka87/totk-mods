// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Hud.hpp"
#include "HudPolicy.hpp"
#include "ChampionHudAssets.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <lib.hpp>

namespace botw_abilities::champions::hud {
namespace {
totk::render::HudFrame g_frame;
std::atomic<bool> g_nativeHudOpen{};
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
struct Guard {
    Guard() { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Guard() { g_lock.clear(std::memory_order_release); }
};
bool read(totk::render::HudFrame& frame) {
    Guard guard;
    if (!g_nativeHudOpen.load(std::memory_order_acquire)) return false;
    frame = g_frame;
    return frame.count != 0;
}
constexpr totk::render::HudExtension extension() {
    totk::render::HudExtension value;
    static_assert(sizeof(kIcons)/sizeof(kIcons[0]) == totk::render::kExtraHudIcons);
    for (unsigned i = 0; i < value.textures.size(); ++i) {
        const auto& source = kIcons[i];
        value.textures[i] = {source.data,source.bytes,source.width,source.height,source.format,
            {source.swizzle[0],source.swizzle[1],source.swizzle[2],source.swizzle[3]}};
    }
    value.read = read;
    return value;
}
constexpr auto g_extension = extension();
HOOK_DEFINE_TRAMPOLINE(StatusUpdate) {
    static std::uintptr_t Callback(const void* screen) {
        const auto result = Orig(screen);
        const auto* bytes = static_cast<const unsigned char*>(screen);
        g_nativeHudOpen.store(bytes[366] == 2 && !(bytes[365] & 0x80),std::memory_order_release);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(StatusClose) {
    static std::uintptr_t Callback(const void* screen) {
        g_nativeHudOpen.store(false,std::memory_order_release);
        return Orig(screen);
    }
};
} // namespace
void hide() { Guard guard; g_frame = {}; }
void reset() { g_nativeHudOpen.store(false,std::memory_order_release); hide(); }
void service(const SavedSnapshot& saved, const std::array<Context,4>& contexts) {
    const auto frame = saved.ready ? makeFrame(saved.states,contexts) : totk::render::HudFrame{};
    Guard guard;
    g_frame = frame;
}
bool install(std::uintptr_t base) {
    constexpr totk::engine::CodeSite sites[]{
        {0xD832C8,0xD10303FF,0x6D0523E9}, {0x1B0D124,0xA9BE7BFD,0xF9000BF3}};
    if (!totk::engine::codeMatches(base,sites,"champions","HUD_INSTALL_REFUSED")) return false;
    StatusUpdate::InstallAtOffset(0xD832C8);
    StatusClose::InstallAtOffset(0x1B0D124);
    const bool ready = totk::render::registerHudExtension(g_extension);
    Logging.Log("[champions] HUD_REGISTERED ready=%u icons=4",unsigned(ready));
    return ready;
}
} // namespace botw_abilities::champions::hud
