// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include "totk/engine/AbilityChoice.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <array>

using namespace totk::engine;

TEST_CASE("a site matches its vanilla words or another mod's near hook") {
    std::array<std::uint32_t, 4> words{0xd102c3ff, 0xa9057bfd, 0xd10743ff, 0xa9187bfd};
    const auto base = reinterpret_cast<std::uintptr_t>(words.data());
    const std::array<CodeSite, 2> sites{{{0, 0xd102c3ff, 0xa9057bfd}, {8, 0xd10743ff, 0}}};
    CHECK(codeMatches(base, sites, "test", "BUILD_REFUSED"));
    words[0] = 0x17ffff00; // backward near jump into a hook
    words[2] = 0x14000040; // forward near jump into a hook
    CHECK(codeMatches(base, sites, "test", "BUILD_REFUSED"));
    words[1] = 0xd503201f; // the rest of the function changed
    CHECK_FALSE(codeMatches(base, sites, "test", "BUILD_REFUSED"));
    words[1] = 0xa9057bfd;
    words[0] = 0x58000051; // a far hook rewrites several words, so it is not accepted
    CHECK_FALSE(codeMatches(base, sites, "test", "BUILD_REFUSED"));
}

TEST_CASE("choosing one ability is seen once by the others and never by itself") {
    AbilityChoiceWatch stasis, bomb, cryonis;
    chooseAbility(Ability::RemoteBomb);
    CHECK(stasis.chosenElsewhere(Ability::Stasis));
    CHECK_FALSE(stasis.chosenElsewhere(Ability::Stasis));
    CHECK_FALSE(bomb.chosenElsewhere(Ability::RemoteBomb));
    CHECK(cryonis.chosenElsewhere(Ability::Cryonis));
    chooseAbility(Ability::RemoteBomb);
    CHECK(cryonis.chosenElsewhere(Ability::Cryonis));
    chooseAbility(Ability::Cryonis);
    CHECK(bomb.chosenElsewhere(Ability::RemoteBomb));
    CHECK_FALSE(cryonis.chosenElsewhere(Ability::Cryonis));
}

TEST_CASE("an ability holding L is seen by the others until it lets go") {
    CHECK_FALSE(lHeldElsewhere(Ability::Cryonis));
    holdL(Ability::RemoteBomb, true);
    CHECK(lHeldElsewhere(Ability::Cryonis));
    CHECK_FALSE(lHeldElsewhere(Ability::RemoteBomb));
    holdL(Ability::Cryonis, false); // only the holder can let go
    CHECK(lHeldElsewhere(Ability::Cryonis));
    holdL(Ability::RemoteBomb, false);
    CHECK_FALSE(lHeldElsewhere(Ability::Cryonis));
}
