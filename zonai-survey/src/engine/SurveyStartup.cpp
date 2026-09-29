// SPDX-License-Identifier: MIT
#include "SurveyStartup.hpp"

#include <lib.hpp>
#include "SurveyGameProfiles.hpp"
#include "totk/engine/Totk121Offsets.hpp"

namespace zonai_survey::engine {


bool startupImageSupported(std::uintptr_t mainBase) {
    const auto& text = exl::util::GetMainModuleInfo().m_Text;
    if (mainBase < text.m_Start || mainBase >= text.GetEnd()) {
        Logging.Log("[survey-startup] main image outside its text segment; Survey stays off");
        return false;
    }
    const auto textSize = text.GetEnd() - mainBase;
    const auto* game = profiles::select(textSize, [mainBase](std::ptrdiff_t offset) {
        return *reinterpret_cast<const std::uint32_t*>(mainBase + offset);
    });
    if (!game) {
        Logging.Log("[survey-startup] unsupported game version; Survey stays off");
        return false;
    }
    if (static_cast<std::size_t>(game->npad.offset) + 8 > textSize) {
        Logging.Log("[survey-startup] Npad outside text version=%s offset=%lx size=%lx",
                    game->version, game->npad.offset, textSize);
        return false;
    }
    const auto* npad = reinterpret_cast<const std::uint32_t*>(mainBase + game->npad.offset);
    if (npad[0] != game->npad.first || npad[1] != game->npad.second) {
        Logging.Log("[survey-startup] Npad entry changed version=%s words=%08x,%08x",
                    game->version, npad[0], npad[1]);
        return false;
    }
    profiles::active = game;
    using totk::engine::Totk121Offsets;
    Totk121Offsets::kSceneModuleInstance.value = game->variables.sceneModule;
    Totk121Offsets::kProcessManagerIndirect.value = game->process.managerGot;
    Totk121Offsets::kProcessListLock.value = game->process.lock;
    Totk121Offsets::kProcessListUnlock.value = game->process.unlock;
    Totk121Offsets::kNpadCalc.value = game->npad.offset;
    totk::engine::layout::kActorNamePointer = game->layout.actorName;
    totk::engine::layout::kActorPosition = game->layout.actorPosition;
    totk::engine::layout::kActorRotation = game->layout.actorRotation;
    Logging.Log("[survey-startup] selected game %s", game->version);
    return true;
}

}
