#include "RecallStamina.hpp"
#include "doctest.h"
#include <limits>
using namespace self_recall::pure;

TEST_CASE("Recall can spend a partial wheel or bonus stamina and refuses exhausted or invalid values") {
    CHECK(staminaStatus(0.01f, 0) == StaminaStatus::Available);
    CHECK(staminaStatus(0, 0.01f) == StaminaStatus::Available);
    CHECK(staminaStatus(0, 0) == StaminaStatus::Empty);
    CHECK(staminaStatus(-1, 100) == StaminaStatus::Unavailable);
    CHECK(staminaStatus(100, std::numeric_limits<float>::infinity()) == StaminaStatus::Unavailable);
    CHECK(staminaStatus(std::numeric_limits<float>::quiet_NaN(), 0) == StaminaStatus::Unavailable);
}

TEST_CASE("release damage protection expires and cannot cross a clock reset") {
    constexpr std::uint64_t released = 9000000000;
    constexpr auto until = released + kRecallReleaseProtectionNs;
    CHECK(releaseProtectionActive(released, until));
    CHECK(releaseProtectionActive(until - 1, until));
    CHECK_FALSE(releaseProtectionActive(until, until));
    CHECK_FALSE(releaseProtectionActive(until + 1, until));
    CHECK_FALSE(releaseProtectionActive(1, until));
    CHECK_FALSE(releaseProtectionActive(released, 0));
}
