#include "doctest.h"
#include "original_charge_fixture.hpp"
#include <bit>
#include <cmath>
#include <cstdint>

static bool withinUlps(float actual, float expected, std::int64_t tolerance = 2) {
    if (actual == expected)
        return true;
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::signbit(actual) != std::signbit(expected))
        return false;
    const auto left = static_cast<std::int64_t>(std::bit_cast<std::int32_t>(actual));
    const auto right = static_cast<std::int64_t>(std::bit_cast<std::int32_t>(expected));
    const auto distance = left > right ? left - right : right - left;
    return distance <= tolerance;
}

static void checkVector(stasis::Vec3 actual, stasis::Vec3 expected) {
    CHECK(withinUlps(actual.x, expected.x));
    CHECK(withinUlps(actual.y, expected.y));
    CHECK(withinUlps(actual.z, expected.z));
}

TEST_CASE("Charge agrees with independently executed original ARM instructions") {
    stasis::ChargeState state;
    for (const auto &step : originalChargeSteps) {
        INFO(step.name);
        INFO("strike counter before step: ", static_cast<unsigned>(state.counter));
        if (step.reset)
            state = {};
        const bool queued = stasis::acceptStrike(state, step.input, step.actorScalar, true);
        checkVector(state.vector, step.vector);
        checkVector(state.point, step.point);
        CHECK(static_cast<unsigned>(state.counter) == step.counter);
        CHECK(state.charged == step.charged);
        CHECK(state.firstEffectPending == step.firstEffectPending);
        CHECK(queued == step.queuedDamage);
    }
}
