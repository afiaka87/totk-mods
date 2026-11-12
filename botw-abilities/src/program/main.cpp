// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <lib.hpp>
#include "BombInstall.hpp"
#include "CryonisInstall.hpp"
#include "StasisInstall.hpp"
#include "wheel/WheelPage.hpp"
#include "champions/Audio.hpp"
#include "champions/Runtime.hpp"
#include "effects/PrivateEffects.hpp"
#include "wolf/Runtime.hpp"
#include "totk/port/CodeCheck.hpp"
#include "totk/suite/Member.hpp"

namespace botw_abilities::suite {

// Shared by the standalone module and Zonai Fieldworks. Shared hooks stack in installation order, so
// Stasis sees controller input first. Offsets are written as 1.2.1 places; totk/port serves 1.4.3.
bool install(std::uintptr_t base) {
    if (!totk::port::supported()) return false;
    const bool privateEffects = botw_abilities::effects::install(base);
    const bool stasis = stasis::install(base);
    const bool remoteBomb = remote_bomb::install(base);
    const bool cryonis = cryonis::install(base);
#ifdef BOTW_ABILITIES_WOLF_DIAGNOSTIC
    const bool wolfReady = botw_abilities::wolf::install(base);
    const auto wolf = wolfReady ? botw_abilities::wolf::chooseFromWheel : nullptr;
    const auto wolfCaption = wolfReady ? botw_abilities::wolf::wheelCaption : nullptr;
    const auto wolfAvailable = wolfReady ? botw_abilities::wolf::canSummon : nullptr;
#else
    constexpr botw_abilities::wheel::Choose wolf = nullptr;
    constexpr botw_abilities::wheel::Caption wolfCaption = nullptr;
    constexpr botw_abilities::wheel::Available wolfAvailable = nullptr;
#endif
    bool page = false;
    {
        // Zonai Fieldworks owns the wheel's paging for its own page, so these hooks stay on.
        TOTK_SUITE_ALWAYS_ON();
        page = botw_abilities::wheel::install(base, stasis ? stasis::chooseFromWheel : nullptr,
                                              cryonis ? cryonis::chooseFromWheel : nullptr,
                                              remoteBomb ? remote_bomb::chooseSphereFromWheel : nullptr,
                                              remoteBomb ? remote_bomb::chooseCubeFromWheel : nullptr, wolf,
                                              wolfCaption, wolfAvailable);
    }
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
    return true;
}

#ifdef TOTK_SUITE_MEMBER
// Fieldworks switch (controller thread): off ends each ability and removes its world objects; items stay.
void enabled(bool on) {
    stasis::switched(on);
    remote_bomb::switched(on);
    cryonis::switched(on);
    botw_abilities::champions::switched(on);
#ifdef BOTW_ABILITIES_WOLF_DIAGNOSTIC
    botw_abilities::wolf::switched(on);
#endif
    Logging.Log("[botw-abilities] SWITCHED %s", on ? "on" : "off");
}
#endif

}  // namespace botw_abilities::suite

#ifndef TOTK_SUITE_MEMBER
extern "C" void exl_main(void *, void *) {
    exl::hook::Initialize();
    if (!totk::port::start("botw-abilities")) return;
    (void)botw_abilities::suite::install(exl::util::modules::GetTargetStart());
}
extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
#endif
