// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>

#include "../src/include/arrowbound/GameProfiles.hpp"

#include <map>

using namespace arrowbound::profiles;

namespace {
using Words = std::map<std::ptrdiff_t, std::uint32_t>;
Words identityOf(const Game& game) {
    Words words;
    for (const auto& site : game.identity) words[site.offset] = site.word;
    return words;
}
const Game* pick(const Words& words, std::size_t textSize = 0x4000000) {
    return select(textSize, [&words](std::ptrdiff_t at) {
        const auto it = words.find(at);
        return it == words.end() ? 0u : it->second;
    });
}
}  // namespace

TEST_CASE("Each build's identity selects only that build") {
    for (const auto& game : kGames) {
        CAPTURE(game.name);
        CHECK(pick(identityOf(game)) == &game);
    }
}

TEST_CASE("A changed word, a short image, or two matching builds select nothing") {
    for (const auto& game : kGames) {
        CAPTURE(game.name);
        for (const auto& site : game.identity) {
            auto words = identityOf(game);
            words[site.offset] ^= 1;
            CHECK(pick(words) == nullptr);
        }
        CHECK(pick(identityOf(game), std::size_t(game.identity[0].offset)) == nullptr);
    }
    auto both = identityOf(kGames[0]);
    const auto other = identityOf(kGames[5]);
    both.insert(other.begin(), other.end());
    REQUIRE(both.size() == 12);
    CHECK(pick(both) == nullptr);
}

TEST_CASE("Profiles keep the shape the runtime depends on") {
    for (const auto& game : kGames) {
        CAPTURE(game.name);
        const bool newer = game.newRenderer();
        CHECK((game.physics.velocityClamp.offset != 0) == !newer);
        CHECK((game.physics.requestVelocity.offset != 0) == newer);
        CHECK((game.physics.requestVelocityWrapper.offset != 0) == newer);
        // 1.4.x's clock runs from the frame-rate update; the delta-frame entry is off its main path.
        CHECK((game.hooks.frameRate.offset != 0) == newer);
        // 1.4.1+ pass a result flag before the pouch selection's category and index.
        const bool flagForm = newer && game.version != GameVersion::V140;
        CHECK((game.hooks.pouchSelectArgs.word == kPouchSelectFlagForm) == flagForm);
        CHECK(game.hooks.pouchSelectArgs.offset == game.hooks.pouchSelect.offset + 0x20);
        CHECK((game.calls.getRagdoll != 0) == !newer);
        CHECK((game.calls.isPouchUser != 0) == !newer);
        CHECK((game.calls.pouchFlagHelper != 0) == newer);
        CHECK(game.hooks.flightClock.word == 0xF9400828);
        CHECK(game.variables.handleStrideSlot == game.variables.handleTableSlot + 8);
    }
}
