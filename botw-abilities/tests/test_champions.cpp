#include "doctest.h"
#include "Gale.hpp"
#include "SageProgress.hpp"
#include <limits>

using namespace botw_abilities::champions;

namespace {
constexpr Context live{true, false, false, false};
constexpr std::array<Blessing, 4> blessings{
    Blessing::Mipha, Blessing::Revali, Blessing::Daruk, Blessing::Urbosa};

State exhaust(Blessing blessing) {
    auto state = initial(blessing);
    while (state.charges) state = consume(blessing, state, live).state;
    return state;
}
} // namespace

TEST_CASE("Blessings use the release charge counts and BotW's loaded recharge durations") {
    constexpr std::array<int, 4> charges{1, 3, 6, 3};
    constexpr std::array<float, 4> durations{1440.0f, 360.0f, 1080.0f, 720.0f};
    for (std::size_t i = 0; i < blessings.size(); ++i) {
        CHECK(rule(blessings[i]).charges == charges[i]);
        CHECK(rule(blessings[i]).rechargeSeconds == durations[i]);
        CHECK(valid(blessings[i], initial(blessings[i])));
    }
}

TEST_CASE("Each blessing accepts either matching vow without borrowing another region's progress") {
    for (auto blessing : blessings) {
        const auto flags = kVowFlags[static_cast<std::size_t>(blessing)];
        CHECK_FALSE(hasSageVow(blessing, [](auto) { return false; }));
        CHECK(hasSageVow(blessing, [flags](auto hash) { return hash == flags.original; }));
        CHECK(hasSageVow(blessing, [flags](auto hash) { return hash == flags.upgraded; }));
        for (auto other : blessings) {
            if (other == blessing) continue;
            const auto otherFlags = kVowFlags[static_cast<std::size_t>(other)];
            CHECK_FALSE(hasSageVow(blessing, [otherFlags](auto hash) {
                return hash == otherFlags.original || hash == otherFlags.upgraded;
            }));
        }
    }
}

TEST_CASE("Only the last use starts recharge and completion restores the entire charge pool") {
    for (auto blessing : blessings) {
        auto state = initial(blessing);
        for (unsigned count = rule(blessing).charges; count; --count) {
            const auto used = consume(blessing, state, live);
            REQUIRE(used.consumed);
            state = used.state;
            CHECK(state.charges == count - 1);
            CHECK(valid(blessing, state));
            if (state.charges) {
                CHECK(state.remainingSeconds == 0.0f);
                CHECK_FALSE(advance(blessing, state, live, 100000.0f).completed);
            }
        }
        const auto rejected = consume(blessing, state, live);
        CHECK_FALSE(rejected.consumed);
        CHECK(rejected.refusal == Refusal::Recharging);
        CHECK(rejected.state.remainingSeconds == rule(blessing).rechargeSeconds);
        const auto almost = advance(blessing, state, live, rule(blessing).rechargeSeconds - 0.5f);
        CHECK_FALSE(almost.completed);
        CHECK(almost.state.charges == 0);
        const auto ready = advance(blessing, almost.state, live, 0.5f);
        CHECK(ready.completed);
        CHECK(ready.state.charges == rule(blessing).charges);
        CHECK(ready.state.remainingSeconds == 0.0f);
        CHECK_FALSE(advance(blessing, ready.state, live, 0.5f).completed);
    }
}

TEST_CASE("Normal and Ballad times use castle multipliers for each blessing") {
    for (auto blessing : blessings) {
        for (bool ballad : {false, true}) {
            for (bool castle : {false, true}) {
                const Context context{true, false, ballad, castle};
                const float expected = ballad ? (castle ? 9.0f : 3.0f) : (castle ? 3.0f : 1.0f);
                CHECK(rechargeRate(context) == expected);
                auto state = exhaust(blessing);
                const auto first = advance(blessing, state, context, 1.0f);
                CHECK(first.state.remainingSeconds == rule(blessing).rechargeSeconds - expected);
                CHECK(advance(blessing, state, context,
                              rule(blessing).rechargeSeconds / expected + 0.01f).completed);
            }
        }
    }
}

TEST_CASE("Rate changes preserve accumulated recharge and menus stop the clock") {
    const auto spent = exhaust(Blessing::Mipha);
    auto result = advance(Blessing::Mipha, spent, live, 10.0f);
    result = advance(Blessing::Mipha, result.state, {true, false, true, false}, 10.0f);
    result = advance(Blessing::Mipha, result.state, {true, false, true, true}, 10.0f);
    CHECK(result.state.remainingSeconds == 1310.0f);
    CHECK(advance(Blessing::Mipha, result.state, {false, false, true, true}, 9999.0f)
              .state.remainingSeconds == 1310.0f);
    for (float delta : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()})
        CHECK(advance(Blessing::Mipha, result.state, live, delta).state.remainingSeconds == 1310.0f);
}

TEST_CASE("Switch and shrine gates preserve native behavior without consuming charges") {
    for (auto blessing : blessings) {
        auto state = initial(blessing);
        CHECK_FALSE(consume(blessing, state, {true, true, false, false}).consumed);
        CHECK_FALSE(consume(blessing, state, {}).consumed);
        state.enabled = false;
        CHECK_FALSE(consume(blessing, state, live).consumed);
    }
    CHECK(revival(initial(Blessing::Mipha), live, true, false) == Revive::Mipha);
    CHECK(revival(initial(Blessing::Mipha), live, true, true) == Revive::Native);
    CHECK(revival(initial(Blessing::Mipha), live, false, false) == Revive::Native);
    CHECK(revival(exhaust(Blessing::Mipha), live, true, false) == Revive::Native);
    CHECK(revival(initial(Blessing::Mipha), {true, true}, true, false) == Revive::Native);
    CHECK(barrier(initial(Blessing::Daruk), live, true));
    CHECK_FALSE(barrier(initial(Blessing::Daruk), live, false));
    CHECK_FALSE(barrier(initial(Blessing::Daruk), {true, true}, true));
    CHECK(fury(initial(Blessing::Urbosa), live, true));
    CHECK_FALSE(fury(initial(Blessing::Urbosa), live, false));
    CHECK_FALSE(fury(initial(Blessing::Urbosa), {true, true}, true));
}

TEST_CASE("Saved states reject inconsistent charge and timer combinations") {
    for (auto blessing : blessings) {
        CHECK_FALSE(valid(blessing, {0, 0.0f, true}));
        CHECK_FALSE(valid(blessing, {1, 1.0f, true}));
        CHECK_FALSE(valid(blessing, {99, 0.0f, true}));
        CHECK_FALSE(valid(blessing, {0, -1.0f, true}));
        CHECK_FALSE(valid(blessing, {0, rule(blessing).rechargeSeconds + 1.0f, true}));
        CHECK_FALSE(valid(blessing, {0, std::numeric_limits<float>::quiet_NaN(), true}));
    }
}

TEST_CASE("Gale begins after a held jump lands and the running skid stops") {
    const auto charges = initial(Blessing::Revali);
    auto result = stepGale({}, charges, live, {true, true}, 1.0f);
    CHECK(result.state.phase == GalePhase::Jumping);
    CHECK_FALSE(result.beginChannel);
    result = stepGale(result.state, charges, live, {false, true, false, false}, 1.0f);
    CHECK(result.state.phase == GalePhase::Jumping);
    result = stepGale(result.state, charges, live, {false, true, true, false}, 1.0f);
    CHECK(result.state.phase == GalePhase::Settling);
    CHECK_FALSE(result.beginChannel);
    result = stepGale(result.state, charges, live, {false, true, true, true}, 1.0f);
    CHECK(result.beginChannel);
    CHECK(result.state.phase == GalePhase::Charging);
    CHECK_FALSE(result.launch);
    result = stepGale(result.state, charges, live, {false, true, true, true, false, 0.5f}, 1.0f);
    CHECK_FALSE(result.becameReady);
    result = stepGale(result.state, charges, live, {false, true, true, true, false, 0.5f}, 1.0f);
    CHECK(result.becameReady);
    result = stepGale(result.state, charges, live, {false, true, true, true, false, 100.0f}, 1.0f);
    CHECK_FALSE(result.becameReady);
    CHECK_FALSE(result.launch);
    result = stepGale(result.state, charges, live, {false, false, true, true}, 1.0f);
    CHECK(result.launch);
    CHECK(result.endChannel);
    CHECK(result.state.phase == GalePhase::Idle);
    CHECK_FALSE(stepGale(result.state, charges, live, {false, false, true, true}, 1.0f).launch);
    CHECK(charges.charges == 3);
}

TEST_CASE("Gale tap and early release stay ordinary jumps with no charge spent") {
    const auto charges = initial(Blessing::Revali);
    auto result = stepGale({}, charges, live, {true, true}, 1.0f);
    result = stepGale(result.state, charges, live, {}, 1.0f);
    CHECK(result.state.phase == GalePhase::Idle);
    CHECK_FALSE(result.launch);
    result = stepGale(result.state, charges, live, {false, true, true, true}, 1.0f);
    CHECK(result.state.phase == GalePhase::Idle);
    result = stepGale({GalePhase::Charging, 0.5f}, charges, live, {false, false, true, true}, 1.0f);
    CHECK(result.endChannel);
    CHECK_FALSE(result.launch);
}

TEST_CASE("Gale pauses in menus and cancels on interruption or loss of eligibility") {
    const auto charges = initial(Blessing::Revali);
    const Gale channel{GalePhase::Charging, 0.5f};
    auto result = stepGale(channel, charges, {}, {false, true, true, true, false, 5.0f}, 1.0f);
    CHECK(result.state.elapsedSeconds == 0.5f);
    CHECK_FALSE(result.becameReady);
    CHECK_FALSE(result.endChannel);
    result = stepGale(channel, charges, live, {false, true, true, true, true}, 1.0f);
    CHECK(result.endChannel);
    CHECK(result.state.phase == GalePhase::Idle);
    result = stepGale(channel, charges, {true, true}, {false, true, true, true}, 1.0f);
    CHECK(result.endChannel);
    result = stepGale({GalePhase::Ready, 1.0f}, charges, live, {false, false, false, true}, 1.0f);
    CHECK_FALSE(result.launch);
}
