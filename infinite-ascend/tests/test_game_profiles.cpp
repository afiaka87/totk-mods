#include <doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>

#include "GameProfiles.hpp"

using zonai_ascend::profiles::GameProfile;
using zonai_ascend::profiles::kGames;

namespace {

using Words = std::map<std::ptrdiff_t, std::uint32_t>;

void addGame(Words& words, const GameProfile& game) {
    for (const auto& site : game.sites) words[site.offset] = site.original;
}

const GameProfile* find(const Words& words, std::size_t textSize) {
    return zonai_ascend::profiles::select(textSize, [&](std::ptrdiff_t at) {
        const auto it = words.find(at);
        return it == words.end() ? std::uint32_t{} : it->second;
    });
}

std::size_t endOf(const GameProfile& game) {
    std::size_t end = 0;
    for (const auto& site : game.sites)
        end = std::max(end, static_cast<std::size_t>(site.offset) + 4);
    return end;
}

}

TEST_CASE("every exact game profile selects its own offsets") {
    for (const auto& game : kGames) {
        Words words;
        addGame(words, game);
        const auto* selected = find(words, endOf(game));
        REQUIRE(selected != nullptr);
        CHECK(std::strcmp(selected->version, game.version) == 0);
        CHECK(std::strcmp(selected->buildId, game.buildId) == 0);
    }
}

TEST_CASE("a changed instruction or truncated text disables all hooks") {
    for (const auto& game : kGames) {
        Words words;
        addGame(words, game);
        words[game.sites.back().offset] ^= 1;
        CHECK(find(words, endOf(game)) == nullptr);

        addGame(words, game);
        CHECK(find(words, endOf(game) - 1) == nullptr);
    }
}

TEST_CASE("unknown and ambiguous images disable all hooks") {
    const Words empty;
    CHECK(find(empty, endOf(kGames.back())) == nullptr);

    Words mixed;
    addGame(mixed, kGames.front());
    addGame(mixed, kGames.back());
    CHECK(find(mixed, endOf(kGames.back())) == nullptr);
}
