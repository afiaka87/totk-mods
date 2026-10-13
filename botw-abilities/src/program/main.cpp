// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <lib.hpp>
#include "BombInstall.hpp"
#include "CryonisInstall.hpp"
#include "StasisInstall.hpp"
#include "wheel/WheelPage.hpp"

// Shared hooks stack in installation order, so Stasis sees controller input first.
extern "C" void exl_main(void *, void *) {
    exl::hook::Initialize();
    const auto base = exl::util::modules::GetTargetStart();
    const bool stasis = stasis::install(base);
    const bool remoteBomb = remote_bomb::install(base);
    const bool cryonis = cryonis::install(base);
    const bool page = botw_abilities::wheel::install(base, stasis ? stasis::chooseFromWheel : nullptr,
                                                     cryonis ? cryonis::chooseFromWheel : nullptr);
    Logging.Log("[botw-abilities] v0.1.3 stasis=%u remote-bomb=%u cryonis=%u wheel-page=%u",
                unsigned(stasis), unsigned(remoteBomb), unsigned(cryonis), unsigned(page));
}
extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
