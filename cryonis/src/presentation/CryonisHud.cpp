// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisHud.hpp"
#include "CryonisPrompt.hpp"
#include "CryonisRuntime.hpp"
#include "NativeFontAtlas.hpp"
#include "totk/ui/NativeFontRenderer.hpp"
#include "totk/engine/ActorRoster.hpp"
#include <lib.hpp>
#include "Totk121Signatures.hpp"
#include <cstring>
namespace cryonis::hud {
namespace {
namespace font = totk::ui::native_font;
bool read(void *, std::uintptr_t address, void *output, std::size_t size) {
    if (!output || !size || !totk::engine::isPlausibleStringAddress(address) || size > 65536 ||
        size > totk::engine::kMaximumMappedAddress - address)
        return false;
    std::memcpy(output, reinterpret_cast<const void *>(address), size);
    return true;
}
void diagnostic(void *, const char *message) { Logging.Log("[cryonis] %s", message); }
font::Frame labels(const Presentation &p, bool aiming) {
    font::Frame frame;
    if (aiming) {
        auto &line = frame.lines[frame.count++];
        line.x = 788;
        line.y = 568;
        line.scale = 1.5f;
        line.face = font::Face::RodinBold;
        std::strcpy(line.text, p.pending                                ? "Freezing"
                               : p.target.valid && p.target.pillar >= 0 ? "Shatter"
                               : p.cooldown > 0                         ? "Recharging"
                                                                        : "Freeze");
        line.rgba =
            p.canPlace && actionReady(p.target, p.cooldown, p.pending) ? 0xffffffff : 0xaadbd6b8;
        auto &second = frame.lines[frame.count++];
        second.x = 756;
        second.y = 620;
        second.scale = 1.0f;
        std::strcpy(second.text, "B / Tap L  Cancel");
        second.rgba = 0xccffffff;
    }
    return frame;
}
HOOK_DEFINE_TRAMPOLINE(UiHook) {
    static std::uint64_t Callback(void *module, void *args) {
        Presentation p;
        font::Frame frame;
        const bool aiming = runtime::presentation(p, true) && p.running && p.aiming;
        engine::NativePrompt prompt(aiming);
        const auto result = Orig(module, args);
        prompt.restore();
        frame = labels(p, aiming);
        font::publish(frame);
        font::drawUi(reinterpret_cast<std::uintptr_t>(module),
                     reinterpret_cast<std::uintptr_t>(args));
        return result;
    }
};
}
void install(std::uintptr_t base) {
    if (!signatures::codeMatches(base, signatures::kUi, "HUD_REFUSED"))
        return;
    engine::configurePrompt(base);
    if (font::configure(base, totk::ui::native_font_data::atlas(),
                        {nullptr, read, diagnostic, nullptr}))
        UiHook::InstallAtOffset(offsets::kUIModuleDraw2D);
}
}
