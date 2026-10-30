// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <ostream>
#include "../src/effects/PrivateEffectResource.hpp"
#include "../src/effects/PrivateEffectBanks.hpp"
#include <array>
using namespace botw_abilities::effects;

TEST_CASE("private bank lookup leaves native and community effect users alone") {
    CHECK(std::string_view(privateBank("EftLoc_BotwDarukImpact")) == "Sage_Soul_Goron");
    CHECK(std::string_view(privateBank("EftLoc_BotwUrbosaRange")) == "Sage_Soul_Gerudo");
    CHECK(std::string_view(privateBank("TotkNativeSphereExplosion")) == "TotkNativeSphere");
    CHECK(std::string_view(privateBank("TotkRemoteBomb")) == "SpObj_TimerBomb_A_01");
    for (const auto user : {"", "Player", "Sage_Soul_Gerudo", "Sunlight_Blade",
                            "EftLoc_BotwDarukImpactOther", "TotkNativeSphere"})
        CHECK(privateBank(user) == nullptr);
}

TEST_CASE("missing bank aliases apply only to the effect manager dictionary") {
    CHECK(std::string_view(missingPrivateBank(0x1000, 0x1000, "EftLoc_BotwUrbosaRange")) == "Sage_Soul_Gerudo");
    CHECK(missingPrivateBank(0x2000, 0x1000, "EftLoc_BotwUrbosaRange") == nullptr);
    CHECK(missingPrivateBank(0, 0, "EftLoc_BotwUrbosaRange") == nullptr);
    CHECK(missingPrivateBank(0x1000, 0x1000, "CommunityEffect") == nullptr);
}

TEST_CASE("private effect resource rejects a damaged or wrong-version file") {
    std::array<std::uint8_t, 16> bytes{'X', 'L', 'N', 'K', 16, 0, 0, 0, 0x24, 0, 0, 0, 1, 2, 3, 4};
    const auto checksum = fingerprint(bytes);
    CHECK(validResource(bytes, checksum));
    CHECK_FALSE(validResource(std::span(bytes).first(8), checksum));
    for (const auto index : {0, 4, 8, 15}) {
        auto damaged = bytes;
        damaged[index] ^= 1;
        CHECK_FALSE(validResource(damaged, checksum));
    }
    bytes[8] = 0x23;
    CHECK_FALSE(validResource(bytes, fingerprint(bytes)));
}

TEST_CASE("private effects use an available native slot without enlarging the pool") {
    CHECK(hasResourceSlot(1, 1, 8));
    CHECK(hasResourceSlot(7, 7, 8));
    CHECK_FALSE(hasResourceSlot(8, 8, 8));
    CHECK_FALSE(hasResourceSlot(0, 0, 8));
    CHECK_FALSE(hasResourceSlot(2, 1, 8));
    CHECK_FALSE(hasResourceSlot(1, 1, 16));
    CHECK_FALSE(hasResourceSlot(-1, -1, 8));
}
