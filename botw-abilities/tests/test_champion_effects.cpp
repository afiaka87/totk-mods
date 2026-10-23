// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "EffectPulse.hpp"
#include "FuryField.hpp"
#include "AudioWork.hpp"
#include <algorithm>
#include <limits>
#include <vector>

using namespace botw_abilities::champions;
using audio::Cue;

TEST_CASE("Every activation schedules its voice without an appearance actor") {
    struct Sink {
        std::vector<Cue> played;
        bool play(Cue cue) { played.push_back(cue); return true; }
        bool stop(Cue) { return true; }
    } sink;
    constexpr std::array voices{Cue::MiphaUsed,Cue::RevaliUsed,Cue::DarukUsed,Cue::UrbosaUsed};
    for (unsigned i = 0; i < voices.size(); ++i) {
        AudioQueue<3> queue;
        sink.played.clear();
        for (auto work : activationAudio(static_cast<Blessing>(i),false))
            if (work) REQUIRE(queue.push(work));
        queue.service(sink);
        CHECK(std::count(sink.played.begin(),sink.played.end(),voices[i]) == 0);
        queue.tick(0.5f);
        queue.service(sink);
        queue.tick(0.5f);
        queue.service(sink);
        CHECK(std::count(sink.played.begin(),sink.played.end(),voices[i]) == 1);
        CHECK(std::count(sink.played.begin(),sink.played.end(),Cue::ActivationReady) == (i == 0 ? 1 : 0));
        queue.clear();
        queue.tick(1.0f);
        queue.service(sink);
        CHECK(std::count(sink.played.begin(),sink.played.end(),voices[i]) == 1);
    }
}

TEST_CASE("Fury release retains the growing field and cancellation permits a fresh activation") {
    CarrierState state;
    state.pulse.hold(true);
    state.created();
    const auto serial = state.pulse.serial;
    float progress = advanceFuryField(0,3,false);
    CHECK(progress == 0);
    CHECK(observeCarrier(state,true).action == CarrierAction::Follow);
    progress = advanceFuryField(progress,0.25f,true);
    CHECK(progress == doctest::Approx(0.25f));
    releaseFuryField(state.pulse);
    state.pulse.hold(false);
    CHECK(state.pulse.serial == serial);
    CHECK(observeCarrier(state,true).action == CarrierAction::Follow);
    CHECK(advanceFuryField(progress,3,true) == 1);
    CHECK(advanceFuryField(progress,-1,true) == progress);
    state.tickWhileVisible(1.1f);
    CHECK(observeCarrier(state,true).action == CarrierAction::Delete);
    state.pulse.hold(true);
    CHECK(state.pulse.serial != serial);
}

TEST_CASE("Fury strike sound waits twelve original frames while charging sound stops immediately") {
    auto work = usedAudio(Blessing::Urbosa,false);
    CHECK(work.stops == AudioWork::bit(Cue::FuryCharge));
    CHECK(work.plays == 0);
    work.tick(0.3f);
    CHECK(work.plays == 0);
    work.tick(0.11f);
    CHECK(work.plays == AudioWork::bit(Cue::FuryStrike));
}

TEST_CASE("Native field lifetime starts after loading and expires once") {
    CarrierState state;
    REQUIRE(state.pulse.start(0.6f));
    CHECK(observeCarrier(state,false).action == CarrierAction::Spawn);
    state.created();
    state.tickWhileVisible(4.0f);
    CHECK(state.pulse.remaining == doctest::Approx(0.6f));
    CHECK(observeCarrier(state,true).action == CarrierAction::Follow);
    state.tickWhileVisible(0.3f);
    CHECK(observeCarrier(state,true).action == CarrierAction::Follow);
    state.tickWhileVisible(0.4f);
    CHECK(observeCarrier(state,true).action == CarrierAction::Delete);
    CHECK(observeCarrier(state,false).action == CarrierAction::None);
}

TEST_CASE("Restarted native fields retain their lifetime until the old actor leaves") {
    CarrierState state;
    REQUIRE(state.pulse.start(1));
    state.created();
    REQUIRE(observeCarrier(state,true).action == CarrierAction::Follow);
    state.tickWhileVisible(0.5f);
    REQUIRE(state.pulse.start(1));
    state.tickWhileVisible(0.4f);
    CHECK(observeCarrier(state,true).action == CarrierAction::Delete);
    state.tickWhileVisible(0.4f);
    CHECK(state.pulse.remaining == 1);
    CHECK(observeCarrier(state,false).action == CarrierAction::Spawn);
    state.created();
    CHECK(observeCarrier(state,true).action == CarrierAction::Follow);
}

TEST_CASE("Effect pulses retain their delay, pause, lifetime and new activation identity") {
    EffectPulse pulse;
    REQUIRE(pulse.start(4, 5));
    const auto serial = pulse.serial;
    pulse.tick(3);
    CHECK_FALSE(pulse.active());
    CHECK(pulse.remaining == 4);
    pulse.tick(0);
    pulse.tick(std::numeric_limits<float>::quiet_NaN());
    CHECK(pulse.delay == 2);
    pulse.tick(3);
    CHECK(pulse.active());
    CHECK(pulse.remaining == 3);
    CHECK_FALSE(pulse.start(-1));
    CHECK(pulse.serial == serial);
    pulse.tick(3);
    CHECK_FALSE(pulse.active());
    REQUIRE(pulse.start(2));
    CHECK(pulse.serial != serial);
    CHECK(pulse.active());
}

TEST_CASE("Effect actors spawn once, follow, expire, and clean up an arrival after expiry") {
    CarrierState state;
    REQUIRE(state.pulse.start(2));
    CHECK(observeCarrier(state, false).action == CarrierAction::Spawn);
    state.created();
    state.tick(0.2f);
    CHECK(observeCarrier(state, false).action == CarrierAction::None);
    CHECK(observeCarrier(state, true).action == CarrierAction::Follow);
    state.tick(2);
    CHECK(observeCarrier(state, true).action == CarrierAction::Delete);
    CHECK(observeCarrier(state, false).action == CarrierAction::None);
    CHECK(observeCarrier(state, true).action == CarrierAction::Delete);
    CarrierState idle;
    CHECK(observeCarrier(idle, false).action == CarrierAction::None);
}

TEST_CASE("Effect restart waits for the old actor to leave and retries a stalled spawn") {
    CarrierState state;
    REQUIRE(state.pulse.start(2)); state.created();
    REQUIRE(observeCarrier(state, true).action == CarrierAction::Follow);
    REQUIRE(state.pulse.start(10));
    CHECK(observeCarrier(state, true).action == CarrierAction::Delete);
    CHECK(observeCarrier(state, false).action == CarrierAction::Spawn);
    state.created(); state.tick(5.1f);
    const auto step = observeCarrier(state, false);
    CHECK(step.timedOut);
    CHECK(step.action == CarrierAction::Spawn);
    state.created();
    CHECK(observeCarrier(state, false).action == CarrierAction::None);
    CHECK(observeCarrier(state, true).action == CarrierAction::Follow);
}

TEST_CASE("A refused voice retries without replaying an already accepted heal sound") {
    struct Sink {
        bool voice = false;
        std::vector<Cue> plays;
        bool play(Cue cue) { if (cue == Cue::MiphaUsed && !voice) return false; plays.push_back(cue); return true; }
        bool stop(Cue) { return true; }
    } sink;
    AudioQueue<2> queue;
    REQUIRE(queue.push({AudioWork::bit(Cue::Heal) | AudioWork::bit(Cue::MiphaUsed)}));
    REQUIRE(queue.push(readyAudio(Blessing::Revali)));
    CHECK_FALSE(queue.push({}));
    queue.service(sink);
    CHECK(sink.plays == std::vector<Cue>{Cue::Heal});
    sink.voice = true;
    queue.tick(0.5f);
    queue.service(sink);
    CHECK(sink.plays == std::vector<Cue>{Cue::Heal, Cue::MiphaUsed, Cue::Ready, Cue::RevaliReady});
    queue.service(sink);
    CHECK(sink.plays.size() == 4);
}

TEST_CASE("A permanently refused one-shot expires instead of blocking later Champion sounds") {
    struct Sink {
        bool launch = false, stops = true;
        std::vector<Cue> plays;
        int stopCalls = 0;
        bool play(Cue cue) { if (cue == Cue::GaleLaunch && !launch) return false; plays.push_back(cue); return true; }
        bool stop(Cue) { ++stopCalls; return stops; }
    } sink;
    AudioQueue<4> queue;
    REQUIRE(queue.push(usedAudio(Blessing::Revali, false)));
    REQUIRE(queue.push(readyAudio(Blessing::Urbosa)));
    queue.tick(0.5f);
    for (std::uint32_t i = 1; i < kAudioPlayAttempts; ++i) CHECK(queue.service(sink) == 0);
    CHECK(sink.plays.empty());
    CHECK(queue.service(sink) == AudioWork::bit(Cue::GaleLaunch));
    CHECK(sink.plays == std::vector<Cue>{Cue::Ready, Cue::UrbosaReady});
    CHECK(queue.service(sink) == 0);

    sink.stops = false;
    REQUIRE(queue.push(usedAudio(Blessing::Urbosa, true)));
    for (std::uint32_t i = 0; i < 2 * kAudioPlayAttempts; ++i) CHECK(queue.service(sink) == 0);
    REQUIRE(queue.push({}));
    REQUIRE(queue.push({}));
    REQUIRE(queue.push({}));
    CHECK_FALSE(queue.push({}));
    sink.stops = true;
    queue.tick(0.4f);
    CHECK(queue.service(sink) == 0);
    REQUIRE(queue.push({}));
}

TEST_CASE("Every Champion announces recharge once, half a simulation second after the chime") {
    struct Sink {
        std::vector<Cue> plays;
        bool play(Cue cue) { plays.push_back(cue); return true; }
        bool stop(Cue) { return true; }
    } sink;
    constexpr std::array voices{Cue::MiphaReady,Cue::RevaliReady,Cue::DarukReady,Cue::UrbosaReady};
    for (unsigned i = 0; i < voices.size(); ++i) {
        auto work = readyAudio(static_cast<Blessing>(i));
        sink.plays.clear();
        CHECK_FALSE(work.service(sink));
        CHECK(sink.plays == std::vector<Cue>{Cue::Ready});
        work.tick(0);
        work.tick(std::numeric_limits<float>::quiet_NaN());
        work.tick(0.25f);
        CHECK_FALSE(work.service(sink));
        CHECK(sink.plays.size() == 1);
        work.tick(0.25f);
        CHECK(work.service(sink));
        CHECK(sink.plays == std::vector<Cue>{Cue::Ready,voices[i]});
        CHECK(work.service(sink));
        CHECK(sink.plays.size() == 2);
    }
}

TEST_CASE("Simultaneous recharge announcements wait for the preceding speech to finish") {
    struct Sink {
        std::vector<Cue> plays;
        bool play(Cue cue) { plays.push_back(cue); return true; }
        bool stop(Cue) { return true; }
    } sink;
    AudioQueue<4> queue;
    REQUIRE(queue.push(readyAudio(Blessing::Mipha,0.75f)));
    REQUIRE(queue.push(readyAudio(Blessing::Daruk,0.5f)));
    queue.service(sink);
    CHECK(sink.plays == std::vector<Cue>{Cue::Ready});
    queue.tickHead(0.5f); queue.service(sink);
    CHECK(sink.plays == std::vector<Cue>{Cue::Ready,Cue::MiphaReady});
    queue.tickHead(0.5f); queue.service(sink);
    CHECK(sink.plays.size() == 2);
    queue.tickHead(0.25f); queue.service(sink);
    CHECK(sink.plays == std::vector<Cue>{Cue::Ready,Cue::MiphaReady,Cue::Ready});
    queue.tickHead(0.5f); queue.service(sink);
    CHECK(sink.plays.back() == Cue::DarukReady);
    queue.tickHead(1); queue.service(sink);
    CHECK(sink.plays.size() == 4);
}

TEST_CASE("Ability sounds preserve their source delays independently of activation voices") {
    constexpr auto bit = AudioWork::bit;
    CHECK(usedAudio(Blessing::Revali, false).plays == bit(Cue::GaleLaunch));
    CHECK(usedAudio(Blessing::Revali, false).stops == bit(Cue::GaleCharge));
    CHECK(usedAudio(Blessing::Urbosa, true).deferred == bit(Cue::FuryStrike));
    CHECK(usedAudio(Blessing::Urbosa, true).stops == bit(Cue::FuryCharge));
    CHECK(usedAudio(Blessing::Daruk, false).plays == bit(Cue::DarukBlock));
    CHECK(usedAudio(Blessing::Daruk, true).plays == bit(Cue::DarukBreak));
    CHECK(usedAudio(Blessing::Daruk, true).stops == bit(Cue::DarukBlock));
    constexpr std::array voices{Cue::MiphaUsed,Cue::RevaliUsed,Cue::DarukUsed,Cue::UrbosaUsed};
    for (unsigned i = 0; i < voices.size(); ++i) {
        auto work = activationVoice(static_cast<Blessing>(i));
        CHECK(work.plays == 0);
        CHECK(work.deferred == bit(voices[i]));
        work.tick(4.0f/30);
        CHECK(work.plays == 0);
        work.tick(2.0f/30);
        CHECK(work.plays == bit(voices[i]));
        CHECK(work.deferred == 0);
    }
}
