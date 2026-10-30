// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <lib.hpp>
#include "BombInstall.hpp"
#include "CryonisInstall.hpp"
#include "StasisInstall.hpp"
#include "wheel/WheelPage.hpp"
#include "champions/Audio.hpp"
#include "champions/Runtime.hpp"
#include "effects/PrivateEffects.hpp"

// Shared hooks stack in installation order, so Stasis sees controller input first.
extern "C" void exl_main(void *, void *) {
    exl::hook::Initialize();
    const auto base = exl::util::modules::GetTargetStart();
    const bool privateEffects = botw_abilities::effects::install(base);
    const bool stasis = stasis::install(base);
    const bool remoteBomb = remote_bomb::install(base);
    const bool cryonis = cryonis::install(base);
    const bool page = botw_abilities::wheel::install(base, stasis ? stasis::chooseFromWheel : nullptr,
                                                     cryonis ? cryonis::chooseFromWheel : nullptr,
                                                     remoteBomb ? remote_bomb::chooseSphereFromWheel : nullptr,
                                                     remoteBomb ? remote_bomb::chooseCubeFromWheel : nullptr);
    const bool championAudio = botw_abilities::champions::audio::install(base);
    const bool champions = championAudio && botw_abilities::champions::install(base);
    Logging.Log("[botw-abilities] suite stasis=%u remote-bomb=%u cryonis=%u wheel-page=%u",
                unsigned(stasis), unsigned(remoteBomb), unsigned(cryonis), unsigned(page));
    Logging.Log("[botw-abilities] champion-audio=%u champion-runtime=%u", unsigned(championAudio), unsigned(champions));
    Logging.Log("[botw-abilities] private-effects=%u", unsigned(privateEffects));
#ifdef BOTW_ABILITIES_NATIVE_SPHERE
    Logging.Log("[botw-abilities] NATIVE_SPHERE native_model_physics_blast=1");
#endif
#ifdef BOTW_ABILITIES_NATIVE_CUBE
    Logging.Log("[botw-abilities] NATIVE_CUBE native_model_physics_blast=1");
#endif
}
extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
