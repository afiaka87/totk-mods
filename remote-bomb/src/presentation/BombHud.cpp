// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombHud.hpp"
#include "BombHudPolicy.hpp"
#include "BombRuntime.hpp"
#include "BombRender.hpp"
#include <lib.hpp>

namespace remote_bomb::hud {
namespace {
hud_policy::Fade shown;
std::uint64_t lastClock{};
HOOK_DEFINE_TRAMPOLINE(UiHook) {
    static std::uint64_t Callback(void *module, void *args) {
        const auto result = Orig(module, args);
        runtime::Presentation p;
        const bool available = runtime::presentation(p);
        const auto view = hud_policy::describe(p.state, available && p.available,
                                               p.remembered, p.running);
        // The game clock counts frames in 32.32 fixed point and stops while paused.
        const float seconds = available && lastClock && p.clock >= lastClock
                                  ? float(double(p.clock - lastClock) / 4294967296.0 / 30.0) : 0.f;
        if (available)
            lastClock = p.clock;
        shown = hud_policy::fade(shown, view, seconds);
        // No text labels: the controls are learned in play.
        render::drawHud(reinterpret_cast<std::uintptr_t>(module),
                        reinterpret_cast<std::uintptr_t>(args), shown.view, shown.alpha);
        return result;
    }
};
}
bool install(std::uintptr_t base) {
    static_cast<void>(base);
    UiHook::InstallAtOffset(0xB0F844);
    Logging.Log("[remote-bomb] HUD_INSTALLED native UI and original rune gauge");
    return true;
}
}
