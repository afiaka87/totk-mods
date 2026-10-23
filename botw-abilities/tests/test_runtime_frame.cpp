// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "RuntimeFrame.hpp"
#include <limits>

using namespace botw_abilities::champions;

TEST_CASE("Runtime context preserves the scene and all shrine castle and Ballad combinations") {
    constexpr std::uint64_t scenes[]{0, 1, 0x123456789a, (1ull << 40) - 1};
    for (const auto scene : scenes) {
        for (unsigned flags = 0; flags < 64; ++flags) {
            std::array<Context, kBlessingCount> contexts{};
            for (unsigned i = 0; i < kBlessingCount; ++i)
                contexts[i] = {true, bool(flags & 1), bool(flags & (4u << i)), bool(flags & 2)};
            const auto packed = packContext(scene, contexts);
            CHECK(packed == (scene | (std::uint64_t(1u | (flags << 1)) << 40)));
            CHECK(contextScene(packed) == scene);
            for (unsigned i = 0; i < kBlessingCount; ++i) {
                const auto decoded = unpackContext(packed, static_cast<Blessing>(i));
                CHECK(decoded.gameplay);
                CHECK(decoded.shrine == contexts[i].shrine);
                CHECK(decoded.castle == contexts[i].castle);
                CHECK(decoded.ballad == contexts[i].ballad);
            }
        }
    }
    CHECK_FALSE(unpackContext(0, Blessing::Mipha).gameplay);
}

TEST_CASE("Runtime simulation time retains fixed point rounding pause and scene reset behavior") {
    constexpr std::uint64_t frame = 1ull << 32;
    CHECK(simulationSeconds(0, false) == 0);
    CHECK(simulationSeconds(frame, false) == 1.0f / 30.0f);
    CHECK(simulationSeconds(30 * frame, false) == 1);
    CHECK(simulationSeconds(frame / 2, false) == 1.0f / 60.0f);
    CHECK(simulationSeconds(30 * frame, true) == 0);
    constexpr auto before = std::numeric_limits<std::uint64_t>::max() - frame;
    constexpr std::uint64_t after = frame - 1;
    CHECK(simulationSeconds(after - before, false) == 2.0f / 30.0f);
}

TEST_CASE("Failed castle and vow reads keep every blessing active at its normal rate") {
    const auto ballad = [](Blessing blessing) { return blessing == Blessing::Revali; };
    const auto failed = gameplayContexts(false, {true, LocationError::Name}, ballad);
    for (unsigned i = 0; i < kBlessingCount; ++i) {
        CHECK(failed[i].gameplay);
        CHECK_FALSE(failed[i].shrine);
        CHECK_FALSE(failed[i].castle);
        CHECK(failed[i].ballad == (i == unsigned(Blessing::Revali)));
        CHECK(admission(initial(static_cast<Blessing>(i)), failed[i]) == Refusal::None);
    }
    CHECK(rechargeRate(failed[0]) == 1.0f);
    CHECK(rechargeRate(failed[1]) == 3.0f);
    const auto castle = gameplayContexts(true, {true}, [](Blessing) { return false; });
    CHECK(castle[2].castle);
    CHECK(castle[2].shrine);
    CHECK(admission(initial(Blessing::Daruk), castle[2]) == Refusal::Shrine);
}
