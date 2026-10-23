// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "AudioCue.hpp"
#include "State.hpp"

namespace botw_abilities::champions {

struct SoundActivity {
    bool gameplay{}, galeChannel{}, updraft{}, furyChannel{}, darukBarrier{}, galeReady{};
};

class SoundEvents {
    bool galeChannel_{};
    bool galeVoice_{}, furyVoice_{}, galeChime_{}, furyChime_{};

    template <class Sink>
    static bool onRisingEdge(bool active, bool& accepted, audio::Cue cue, Sink& sink) {
        if (!active) { accepted = false; return true; }
        if (accepted) return true;
        return accepted = sink.play(cue);
    }

public:
    template <class Sink>
    bool reset(Sink& sink) {
        if (!sink.stopAll()) return false;
        galeChannel_ = false;
        galeVoice_ = furyVoice_ = galeChime_ = furyChime_ = false;
        return true;
    }

    template <class Sink>
    bool update(const SoundActivity& active, Sink& sink) {
        using audio::Cue;
        if (!active.gameplay) return reset(sink);
        bool accepted = true;
        if (active.galeChannel != galeChannel_) {
            const bool changed = active.galeChannel ? sink.play(Cue::GaleCharge) : sink.stop(Cue::GaleCharge);
            if (changed) galeChannel_ = active.galeChannel;
            accepted &= changed;
        }
        accepted &= sink.sustain(Cue::Updraft, active.updraft);
        accepted &= sink.sustain(Cue::FuryCharge, active.furyChannel);
        accepted &= onRisingEdge(active.galeReady,galeVoice_,Cue::RevaliChargeVoice,sink);
        accepted &= onRisingEdge(active.furyChannel,furyVoice_,Cue::UrbosaChargeVoice,sink);
        accepted &= onRisingEdge(active.galeReady,galeChime_,Cue::ActivationReady,sink);
        accepted &= onRisingEdge(active.furyChannel,furyChime_,Cue::ActivationReady,sink);
        // Hits start DarukBlock (Pl_KagoShield_Count03), which loops after its 2.46 s attack, so it stops with the barrier.
        if (!active.darukBarrier) accepted &= sink.stop(Cue::DarukBlock);
        return accepted;
    }

};
} // namespace botw_abilities::champions
