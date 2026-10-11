// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisPrompt.hpp"
#include "CryonisEngine.hpp"
#include "Totk121Signatures.hpp"
#include "Totk121Layout.hpp"
#include <lib.hpp>
namespace cryonis::engine {
namespace {
bool g_promptReady{}, g_promptReported{};
std::uintptr_t follow(std::uintptr_t address) {
    const auto next = read<std::uintptr_t>(address);
    return totk::engine::isPlausibleStringAddress(next) ? next : 0;
}
}
void configurePrompt(std::uintptr_t base) {
    g_promptReady = signatures::codeMatches(base, signatures::kPrompt, "HUD_REFUSED");
}
NativePrompt::NativePrompt(bool hide) {
    if (!hide || !g_promptReady)
        return;
    const auto slot = follow(moduleBase + offsets::kScreenManagerSlot);
    const auto manager = slot ? follow(slot) : 0;
    if (!manager)
        return;
    const auto screen = call<std::uintptr_t (*)(std::uintptr_t, std::uint32_t)>(
        offsets::kGetDoCommandScreen)(manager, offsets::kDoCommandScreenHash);
    if (!totk::engine::isPlausibleStringAddress(screen))
        return;
    const auto paneLayout = follow(screen + layout::prompt::kLayout);
    const auto root = paneLayout ? follow(paneLayout + layout::prompt::kRoot) : 0;
    if (!root)
        return;
    flags = reinterpret_cast<std::uint8_t *>(root + layout::prompt::kFlags);
    visible = *flags & 1;
    *flags &= std::uint8_t(~1u);
    if (!g_promptReported) {
        g_promptReported = true;
        Logging.Log("[cryonis] NATIVE_A_PROMPT_SUPPRESSED");
    }
}
void NativePrompt::restore() {
    if (flags) {
        *flags = std::uint8_t((*flags & ~1u) | visible);
        flags = nullptr;
    }
}
NativePrompt::~NativePrompt() { restore(); }
}
