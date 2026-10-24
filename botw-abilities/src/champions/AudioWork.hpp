// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "AudioCue.hpp"
#include "State.hpp"
#include <cmath>

namespace botw_abilities::champions {
// A one-shot cue refused this many times is dropped so later cues do not wait; loop stops never drop.
inline constexpr std::uint32_t kAudioPlayAttempts = 90;

struct AudioWork {
    std::uint32_t plays = 0, stops = 0, attempts = 0, dropped = 0;
    std::uint32_t deferred = 0;
    float delay = 0;
    float holdSeconds = 0;
    static constexpr std::uint32_t bit(audio::Cue cue) { return 1u << unsigned(cue); }
    explicit operator bool() const { return plays || stops || deferred || holdSeconds > 0; }
    void tick(float seconds) {
        if (!std::isfinite(seconds) || seconds <= 0) return;
        if (!plays && !stops && !deferred)
            holdSeconds = holdSeconds > seconds ? holdSeconds-seconds : 0;
        if (!deferred) return;
        delay -= seconds;
        if (delay <= 0) { plays |= deferred; deferred = 0; }
    }
    template <class Sink> bool service(Sink& sink) {
        for (unsigned i = 0; i < unsigned(audio::Cue::Count); ++i) {
            const auto mask = 1u << i;
            const auto cue = static_cast<audio::Cue>(i);
            if ((stops & mask) && sink.stop(cue)) stops &= ~mask;
        }
        for (unsigned i = 0; i < unsigned(audio::Cue::Count); ++i) {
            const auto mask = 1u << i;
            if ((plays & mask) && sink.play(static_cast<audio::Cue>(i))) plays &= ~mask;
        }
        if (plays && ++attempts >= kAudioPlayAttempts) {
            dropped |= plays;
            plays = 0;
        }
        return !bool(*this);
    }
};

constexpr AudioWork usedAudio(Blessing blessing, bool finalCharge) {
    using audio::Cue;
    constexpr auto bit = AudioWork::bit;
    switch (blessing) {
    case Blessing::Mipha: return {0,0,0,0,bit(Cue::Heal),4.0f/30.0f};
    case Blessing::Revali: return {bit(Cue::GaleLaunch), bit(Cue::GaleCharge)};
    case Blessing::Daruk: return {bit(finalCharge ? Cue::DarukBreak : Cue::DarukBlock) | bit(Cue::ParryImpact),
                                  finalCharge ? bit(Cue::DarukBlock) : 0};
    case Blessing::Urbosa: return {0,bit(Cue::FuryCharge),0,0,bit(Cue::FuryStrike),12.0f/30.0f};
    }
    return {};
}

constexpr AudioWork activationVoice(Blessing blessing) {
    using audio::Cue;
    constexpr std::array voices{Cue::MiphaUsed,Cue::RevaliUsed,Cue::DarukUsed,Cue::UrbosaUsed};
    return {0,0,0,0,AudioWork::bit(voices[unsigned(blessing)]),5.0f/30.0f};
}

constexpr std::array<AudioWork,3> activationAudio(Blessing blessing, bool finalCharge) {
    const AudioWork chime = blessing == Blessing::Mipha
        ? AudioWork{0,0,0,0,AudioWork::bit(audio::Cue::ActivationReady),6.0f/30.0f} : AudioWork{};
    return {usedAudio(blessing,finalCharge),activationVoice(blessing),chime};
}

constexpr audio::Cue readyVoice(Blessing blessing) {
    using audio::Cue;
    constexpr std::array voices{Cue::MiphaReady,Cue::RevaliReady,Cue::DarukReady,Cue::UrbosaReady};
    return voices[unsigned(blessing)];
}

constexpr AudioWork readyAudio(Blessing blessing, float voiceSeconds = 0) {
    return {AudioWork::bit(audio::Cue::Ready),0,0,0,AudioWork::bit(readyVoice(blessing)),0.5f,
            voiceSeconds > 0 ? voiceSeconds : 0};
}

template <std::size_t Capacity> class AudioQueue {
public:
    bool push(AudioWork work) {
        if (count_ == Capacity) return false;
        work_[(head_ + count_) % Capacity] = work;
        ++count_;
        return true;
    }
    // Returns the cues dropped as stale during this service.
    template <class Sink> std::uint32_t service(Sink& sink) {
        std::uint32_t dropped = 0;
        while (count_ && work_[head_].service(sink)) {
            dropped |= work_[head_].dropped;
            head_ = (head_ + 1) % Capacity;
            --count_;
        }
        return dropped;
    }
    void tick(float seconds) {
        for (std::size_t i = 0; i < count_; ++i) work_[(head_ + i) % Capacity].tick(seconds);
    }
    void tickHead(float seconds) { if (count_) work_[head_].tick(seconds); }
    void clear() { head_ = count_ = 0; }
private:
    static_assert(Capacity > 0);
    std::array<AudioWork, Capacity> work_{};
    std::size_t head_ = 0, count_ = 0;
};
} // namespace botw_abilities::champions
