// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
#include <cstring>
#include <iterator>

namespace botw_abilities::wolf {

// The original's attack sounds and the clip frame each starts on (BotW 1.6.0 WolfLink sound user: Attack,
// Attack_ChainAttack and Turn_Attack, plus the chain lunge's action slot). The calls are in the wolf's own
// sound user, built by tools/build_wolf_sound.py; Roar and ChargeFootStep are the stock wolf's.
struct SoundCue { const char* command; const char* call; float frame; };
inline constexpr SoundCue kSoundCues[]{
    {"Turn_Attack", "Roar", 10}, {"Attack", "ChainLarge", 3},
    {"Attack_ChainAttack", "ChainAttackWind", 0}, {"Attack_ChainAttack", "ChargeFootStep", 0},
    {"Attack_ChainAttack", "RoarAttack", 0}};
static_assert(std::size(kSoundCues) <= 32);

// Plays a command's cues as its clip passes their frames. A new command or a stop drops the rest.
class SoundTimeline {
public:
    void start(const char* command, float rate) {
        pending_ = 0;
        for (unsigned i = 0; i < std::size(kSoundCues); ++i)
            if (command && std::strcmp(kSoundCues[i].command, command) == 0) pending_ |= 1u << i;
        frame_ = 0;
        rate_ = rate > 0 ? rate : 1;
    }
    void stop() { pending_ = 0; }
    // Call once per tick of the command, starting with the tick that played it.
    template <class Play> void advance(float delta, Play&& play) {
        for (unsigned i = 0; i < std::size(kSoundCues); ++i) {
            if ((pending_ >> i & 1) && frame_ >= kSoundCues[i].frame) {
                pending_ &= ~(1u << i);
                play(kSoundCues[i]);
            }
        }
        frame_ += delta * rate_;
    }
    [[nodiscard]] bool pending() const { return pending_ != 0; }
private:
    std::uint32_t pending_{};
    float frame_{}, rate_{1};
};

}
