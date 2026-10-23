// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "FatalHealing.hpp"
#include "UseEvents.hpp"
#include <thread>
#include <vector>

using namespace botw_abilities::champions;
namespace {
States full() { return {initial(Blessing::Mipha), initial(Blessing::Revali), initial(Blessing::Daruk), initial(Blessing::Urbosa)}; }
struct Fixture {
    SavedState state;
    Context context{true};
    std::vector<char> calls;
    bool protectedHit = false, healingAccepted = true;
    int events = 0;
    Fixture() { REQUIRE(state.restore(0, full())); }
    FatalResult fatal() {
        return healFatal(state, context, [&] { calls.push_back('N'); return 12; },
            [&] { calls.push_back('P'); return protectedHit; },
            [&] { calls.push_back('H'); return healingAccepted ? HealingResult{120} : HealingResult{0, HealingError::MessageRefused}; },
            [&](std::uint64_t generation) { CHECK(generation == 0); calls.push_back('E'); ++events; });
    }
};
}

TEST_CASE("Mipha preserves one-shot protection before healing or spending a charge") {
    Fixture fixture;
    fixture.protectedHit = true;
    const auto result = fixture.fatal();
    CHECK(result.source == FatalSource::OneShot);
    CHECK(result.health == 1);
    CHECK(fixture.calls == std::vector<char>{'P'});
    CHECK(fixture.state.snapshot().states[0].charges == 1);
}

TEST_CASE("Mipha commits a successful native heal before requesting presentation") {
    Fixture fixture;
    const auto result = fixture.fatal();
    CHECK(result.source == FatalSource::Mipha);
    CHECK(result.charged);
    CHECK(result.health == 120);
    CHECK(fixture.calls == std::vector<char>{'P', 'H', 'E'});
    CHECK(fixture.state.snapshot().states[0].charges == 0);
    CHECK(fixture.state.snapshot().states[0].remainingSeconds == 1440);
    CHECK(fixture.events == 1);
    fixture.calls.clear();
    CHECK(fixture.fatal().source == FatalSource::Native);
    CHECK(fixture.calls == std::vector<char>{'N'});
    CHECK(fixture.events == 1);
}

TEST_CASE("Mipha uses the unchanged native fallback when unavailable") {
    Fixture fixture;
    SUBCASE("shrine") { fixture.context.shrine = true; }
    SUBCASE("paused") { fixture.context.gameplay = false; }
    SUBCASE("switched off") { REQUIRE(fixture.state.enable(0, Blessing::Mipha, false)); }
    SUBCASE("save loading") { fixture.state.invalidate(); }
    const auto result = fixture.fatal();
    CHECK(result.source == FatalSource::Native);
    CHECK(result.health == 12);
    CHECK_FALSE(result.charged);
    CHECK(fixture.calls == std::vector<char>{'N'});
}

TEST_CASE("Mipha refunds a refused healing message and leaves the fairy path available") {
    Fixture fixture;
    fixture.healingAccepted = false;
    const auto result = fixture.fatal();
    CHECK(result.source == FatalSource::Native);
    CHECK(result.healing == HealingError::MessageRefused);
    CHECK(fixture.calls == std::vector<char>{'P', 'H', 'N'});
    CHECK(fixture.state.snapshot().states[0].charges == 1);
    CHECK(fixture.state.snapshot().states[0].remainingSeconds == 0);
    CHECK(fixture.events == 0);
}

TEST_CASE("Reservations are invisible to saves until committed and cannot be finished twice") {
    SavedState state;
    REQUIRE(state.restore(0, full()));
    const auto claim = state.reserve(0, Blessing::Mipha, {true});
    REQUIRE(claim);
    CHECK(state.snapshot().states[0].charges == 1);
    CHECK_FALSE(state.reserve(0, Blessing::Mipha, {true}));
    CHECK_FALSE(state.use(Blessing::Mipha, {true}).consumed);
    REQUIRE(state.enable(0, Blessing::Mipha, false));
    std::array<Context, 4> contexts{}; contexts[0].gameplay = true;
    CHECK(state.tick(contexts, 2000) == 0);
    REQUIRE(state.finish(claim, false));
    CHECK_FALSE(state.finish(claim, false));
    CHECK_FALSE(state.snapshot().states[0].enabled);
    CHECK(state.snapshot().states[0].charges == 1);
    const auto stale = state.reserve(0, Blessing::Daruk, {true});
    REQUIRE(stale);
    const auto generation = state.invalidate();
    auto loaded = full(); loaded[2].charges = 2;
    REQUIRE(state.restore(generation, loaded));
    CHECK_FALSE(state.finish(stale, false));
    CHECK(state.snapshot().states[2].charges == 2);
}

TEST_CASE("A load during healing never spends the new save's charge or queues an old effect") {
    SavedState state;
    REQUIRE(state.restore(0, full()));
    int events = 0;
    const auto result = healFatal(state, {true}, [] { return 12; }, [] { return false; }, [&] {
        const auto generation = state.invalidate();
        REQUIRE(state.restore(generation, full()));
        return HealingResult{120};
    }, [&](std::uint64_t) { ++events; });
    CHECK(result.source == FatalSource::Mipha);
    CHECK_FALSE(result.charged);
    CHECK(state.snapshot().states[0].charges == 1);
    CHECK(events == 0);
}

TEST_CASE("A stale gameplay context cannot revive using a newly loaded save") {
    SavedState state;
    REQUIRE(state.restore(0, full()));
    const auto old = state.snapshot().generation;
    REQUIRE(state.restore(state.invalidate(), full()));
    int native = 0, protectedHits = 0, heals = 0;
    const auto result = healFatal(state, {true}, [&] { ++native; return 12; },
        [&] { ++protectedHits; return false; }, [&] { ++heals; return HealingResult{120}; },
        [](std::uint64_t) {}, old);
    CHECK(result.source == FatalSource::Native);
    CHECK(native == 1);
    CHECK(protectedHits == 0);
    CHECK(heals == 0);
    CHECK(state.snapshot().states[0].charges == 1);
}

TEST_CASE("Native gameplay event queue preserves producers and filters old saves and scenes") {
    UseEvents<32> queue;
    std::thread first([&] { for (int i = 0; i < 8; ++i) queue.push({1, 0x7100010000, Blessing::Daruk, false}); });
    std::thread second([&] { for (int i = 0; i < 8; ++i) queue.push({1, 0x7100010000, Blessing::Urbosa, false}); });
    first.join(); second.join();
    UseEvent event;
    int daruk = 0, urbosa = 0;
    while (queue.pop(1, 0x7100010000, event)) {
        daruk += event.blessing == Blessing::Daruk;
        urbosa += event.blessing == Blessing::Urbosa;
    }
    CHECK(daruk == 8); CHECK(urbosa == 8);
    REQUIRE(queue.push({1, 0x7100010000, Blessing::Mipha, true}));
    REQUIRE(queue.push({2, 0x7100020000, Blessing::Mipha, true}));
    REQUIRE(queue.push({2, 0x7100010000, Blessing::Revali, false}));
    REQUIRE(queue.pop(2, 0x7100010000, event));
    CHECK(event.blessing == Blessing::Revali);
    CHECK_FALSE(queue.pop(2, 0x7100010000, event));
    for (int i = 0; i < 32; ++i) REQUIRE(queue.push({2, 0x7100010000, Blessing::Daruk, false}));
    CHECK_FALSE(queue.push({2, 0x7100010000, Blessing::Mipha, true}));
    queue.clear();
    CHECK_FALSE(queue.pop(2, 0x7100010000, event));
}
