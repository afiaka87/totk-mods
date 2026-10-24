// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

namespace botw_abilities::champions::audio {
enum class Cue : unsigned {
    Ready, Heal, GaleCharge, GaleLaunch, DarukBlock, DarukBreak, FuryCharge, FuryStrike,
    ActivationReady, Updraft, RevaliReady, RevaliUsed, UrbosaReady, UrbosaUsed, MiphaUsed, DarukUsed,
    MiphaReady, DarukReady, RevaliChargeVoice, UrbosaChargeVoice, ParryImpact, Count,
};
static_assert(unsigned(Cue::Count) <= 32);
} // namespace botw_abilities::champions::audio
