// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include <arrowbound/ActiveGame.hpp>

#include <lib.hpp>

#include "../../../runtime-support/include/totk/render/PfxHookSite.hpp"

#include "totk/engine/Totk121Offsets.hpp"

namespace arrowbound::profiles {
namespace {
const Game* g_active = nullptr;
}  // namespace

const Game* active() { return g_active; }

const Game* activate(std::uintptr_t mainBase, std::size_t textSize) {
    const auto* game = select(textSize, [mainBase](std::ptrdiff_t at) {
        return *reinterpret_cast<const std::uint32_t*>(mainBase + at);
    });
    if (!game) return nullptr;
    using totk::engine::Totk121Offsets;
    namespace layout = totk::engine::layout;
    Totk121Offsets::kSceneModuleInstance.value = game->variables.sceneModule;
    Totk121Offsets::kForceSetMatrix.value = game->calls.forceSetMatrix;
    if (game->version != GameVersion::V121) {
        Totk121Offsets::kGetMotionType.value = 0;
        Totk121Offsets::kRequestChangeMotionType.value = 0;
        Totk121Offsets::kRequestSetLinearVelocity.value = 0;
    }
    layout::kActorNamePointer = game->layout.actorName;
    layout::kActorComponentRegistry = game->layout.actorRegistry;
    layout::kActorPosition = game->layout.actorPosition;
    layout::kActorRotation = game->layout.actorRotation;
    layout::kActorLinearVelocity = game->layout.actorVelocity;
    g_active = game;
    return game;
}

bool entryHookable(std::uintptr_t mainBase, const Site& site, const char* label) {
    const auto at = mainBase + site.offset;
    const auto* words = reinterpret_cast<const std::uint32_t*>(at);
    if (words[0] == site.word) return true;
    if (totk::render::decodeDetour(at, words).kind != totk::render::PfxEntryKind::Unsupported)
        return true;
    Logging.Log("[arrowbound] HOOK DISABLED %s: main+%p words %08x,%08x != %08x", label,
                reinterpret_cast<void*>(site.offset), words[0], words[1], site.word);
    return false;
}
}  // namespace arrowbound::profiles
