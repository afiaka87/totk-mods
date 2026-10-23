// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "AudioPlayback.hpp"
#include "AudioTables.hpp"
#include "SoundEvents.hpp"
#include <vector>

namespace {
using namespace botw_abilities::champions::audio;
using namespace botw_abilities::champions;

struct Speaker {
    bool accepts = true;
    std::uint32_t serial{};
    std::vector<Handle> faded;
    int attempts{};
    Handle emit(std::size_t cue) {
        ++attempts;
        return accepts ? Handle{2, 0, static_cast<std::int16_t>(cue), ++serial} : Handle{};
    }
    auto emit() { return [this](std::size_t cue) { return emit(cue); }; }
    auto fade() { return [this](Handle handle) { faded.push_back(handle); }; }
};

struct Sink {
    bool accepts = true;
    std::vector<Cue> started, stopped;
    std::array<bool, unsigned(Cue::Count)> loops{};
    int resets{};
    bool play(Cue cue) { started.push_back(cue); return accepts; }
    bool stop(Cue cue) {
        stopped.push_back(cue);
        if (accepts) loops[unsigned(cue)] = false;
        return accepts;
    }
    bool sustain(Cue cue, bool active) {
        if (accepts) loops[unsigned(cue)] = active;
        return accepts;
    }
    bool stopAll() {
        ++resets;
        if (accepts) loops.fill(false);
        return accepts;
    }
};
}

TEST_CASE("Champion loops retry a refused emission and stop only their own native handle") {
    Playback<15> sounds;
    Speaker speaker;
    speaker.accepts = false;
    CHECK_FALSE(sounds.play(8, true, speaker.emit(), speaker.fade()));
    speaker.accepts = true;
    CHECK(sounds.play(8, true, speaker.emit(), speaker.fade()));
    CHECK(sounds.play(8, true, speaker.emit(), speaker.fade()));
    CHECK(speaker.attempts == 2);
    CHECK(sounds.play(6, true, speaker.emit(), speaker.fade()));
    sounds.stop(8, speaker.fade());
    sounds.stop(8, speaker.fade());
    REQUIRE(speaker.faded.size() == 1);
    CHECK(speaker.faded[0].index == 8);
    CHECK(speaker.faded[0].serial == 1);
    sounds.stopAll(speaker.fade());
    REQUIRE(speaker.faded.size() == 2);
    CHECK(speaker.faded[1].index == 6);
    CHECK(speaker.faded[1].serial == 2);
    CHECK(sounds.play(8, true, speaker.emit(), speaker.fade()));
    CHECK(speaker.attempts == 4);
}

TEST_CASE("A repeated Champion cue replaces the old generation only after its new emission succeeds") {
    Playback<15> sounds;
    Speaker speaker;
    CHECK(sounds.play(4, false, speaker.emit(), speaker.fade()));
    speaker.accepts = false;
    CHECK_FALSE(sounds.play(4, false, speaker.emit(), speaker.fade()));
    CHECK(speaker.faded.empty());
    speaker.accepts = true;
    CHECK(sounds.play(4, false, speaker.emit(), speaker.fade()));
    REQUIRE(speaker.faded.size() == 1);
    CHECK(speaker.faded[0].serial == 1);
    sounds.stopAll(speaker.fade());
    REQUIRE(speaker.faded.size() == 2);
    CHECK(speaker.faded[1].serial == 2);
    CHECK_FALSE(sounds.play(15, false, speaker.emit(), speaker.fade()));
    sounds.stop(15, speaker.fade());
    CHECK(speaker.attempts == 3);
}

TEST_CASE("All fifteen Champion cues retain independent handles and end on scene cleanup") {
    Playback<15> sounds;
    Speaker speaker;
    for (std::size_t cue = 0; cue < 15; ++cue)
        REQUIRE(sounds.play(cue, false, speaker.emit(), speaker.fade()));
    sounds.stopAll(speaker.fade());
    sounds.stopAll(speaker.fade());
    REQUIRE(speaker.faded.size() == 15);
    for (std::size_t cue = 0; cue < 15; ++cue) {
        CHECK(speaker.faded[cue].index == cue);
        CHECK(speaker.faded[cue].serial == cue + 1);
    }
}

TEST_CASE("Channel audio retries refusals without replaying on every gameplay frame") {
    SoundEvents sounds;
    Sink sink;
    const SoundActivity active{true, true, true, true, true};
    sink.accepts = false;
    CHECK_FALSE(sounds.update(active, sink));
    sink.accepts = true;
    CHECK(sounds.update(active, sink));
    CHECK(sounds.update(active, sink));
    CHECK(sink.started == std::vector<Cue>{Cue::GaleCharge, Cue::UrbosaChargeVoice, Cue::ActivationReady,
                                         Cue::GaleCharge, Cue::UrbosaChargeVoice, Cue::ActivationReady});
    CHECK(sink.loops[unsigned(Cue::Updraft)]);
    CHECK_FALSE(sink.loops[unsigned(Cue::DarukBlock)]);
    CHECK(sink.loops[unsigned(Cue::FuryCharge)]);
    CHECK(sounds.update({true, false, false, false}, sink));
    CHECK(sink.stopped == std::vector<Cue>{Cue::GaleCharge, Cue::DarukBlock});
    for (bool loop : sink.loops) CHECK_FALSE(loop);
    CHECK(sounds.update(active, sink));
    CHECK(sink.started.size() == 9);
    sink.accepts = false;
    CHECK_FALSE(sounds.update({}, sink));
    sink.accepts = true;
    CHECK(sounds.update({}, sink));
    for (bool loop : sink.loops) CHECK_FALSE(loop);
    CHECK(sounds.update(active, sink));
    CHECK(sink.started.size() == 12);
}

TEST_CASE("Daruk's looping hit sound keeps playing while the barrier is up and ends with it") {
    SoundEvents sounds;
    Sink sink;
    REQUIRE(sink.play(Cue::DarukBlock));
    REQUIRE(sounds.update({true, false, false, false, true}, sink));
    CHECK(sink.started == std::vector<Cue>{Cue::DarukBlock});
    CHECK(sink.stopped.empty());
    CHECK_FALSE(sink.loops[unsigned(Cue::DarukBlock)]);
    REQUIRE(sounds.update({true}, sink));
    CHECK(sink.stopped == std::vector<Cue>{Cue::DarukBlock});
    sink.accepts = false;
    CHECK_FALSE(sounds.update({true}, sink));
    sink.accepts = true;
    CHECK(sounds.update({true}, sink));
    CHECK(sink.stopped.size() == 3);
}

TEST_CASE("Native sound calls accept the resource converter four-byte alignment") {
    CHECK(donorTableAddress(0x456324));
    CHECK(donorTableAddress(0x457168));
    CHECK_FALSE(donorTableAddress(0));
    CHECK_FALSE(donorTableAddress(0xFFC));
    CHECK_FALSE(donorTableAddress(0x456322));
    CHECK_FALSE(donorTableAddress(1ull << 40));

}

TEST_CASE("Repeated audio refusals log the first few and then sparsely") {
    RefusalCounter counter;
    unsigned reported = 0;
    for (unsigned i = 0; i < 4096; ++i) reported += counter.report();
    CHECK(reported == 8);
    CHECK(counter.count == 4096);
}
