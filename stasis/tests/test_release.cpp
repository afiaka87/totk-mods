#include "doctest.h"
#include "original_release_fixture.hpp"
#include <bit>
#include <cmath>
static bool closeFloat(float a, float b) {
    if (a == b)
        return true;
    if (!std::isfinite(a) || !std::isfinite(b) || std::signbit(a) != std::signbit(b))
        return false;
    const auto x = static_cast<std::int64_t>(std::bit_cast<std::int32_t>(a));
    const auto y = static_cast<std::int64_t>(std::bit_cast<std::int32_t>(b));
    return std::abs(x - y) <= 2;
}
TEST_CASE("Ratio and released impulse agree with original ARM; stored charge survives") {
    for (const auto &row : originalReleaseCases) {
        INFO(row.name);
        stasis::ChargeState state;
        state.vector = row.input;
        state.charged = row.charged;
        state.queuedDamageCount = row.queued;
        CHECK(closeFloat(stasis::chargeRatio(state, row.mass, row.player), row.ratio));
        const auto impulse = stasis::releaseImpulse(state, row.mass, row.player);
        CHECK(closeFloat(impulse.x, row.impulse.x));
        CHECK(closeFloat(impulse.y, row.impulse.y));
        CHECK(closeFloat(impulse.z, row.impulse.z));
        CHECK(state.vector.x == row.storedAfter.x);
        CHECK(state.vector.y == row.storedAfter.y);
        CHECK(state.vector.z == row.storedAfter.z);
    }
}
