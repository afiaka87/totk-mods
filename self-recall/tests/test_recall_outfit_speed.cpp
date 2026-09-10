#include <doctest.h>
#include "RecallOutfitSpeed.hpp"

using namespace self_recall::pure;

TEST_CASE("Glide speed counts any combination of worn slots") {
    OutfitSpeed speed;
    const std::array expected{PlaybackRate::X125, PlaybackRate::X150,
        PlaybackRate::X150, PlaybackRate::X200, PlaybackRate::X150,
        PlaybackRate::X200, PlaybackRate::X200, PlaybackRate::X400};
    for (std::uint8_t mask = 0; mask < 8; ++mask) {
        REQUIRE(speed.update({mask, {101, 202, 303}}));
        CHECK(speed.rate() == expected[mask]);
        CHECK_FALSE(speed.update({mask, {101, 202, 303}}));
    }
}

TEST_CASE("equipping removing and replacing a counted piece reports the resulting speed once") {
    OutfitSpeed speed;
    CHECK(speed.rate() == PlaybackRate::X125);
    REQUIRE(speed.update({0, {101, 202, 303}}));
    CHECK_FALSE(speed.update({0, {401, 502, 603}})); // unrelated outfit
    REQUIRE(speed.update({1, {701, 502, 603}}));
    CHECK(speed.rate() == PlaybackRate::X150);
    CHECK_FALSE(speed.update({1, {701, 802, 903}})); // unrelated upper/lower
    REQUIRE(speed.update({1, {1001, 802, 903}})); // different upgrade of counted head
    CHECK(speed.rate() == PlaybackRate::X150);
    REQUIRE(speed.update({7, {1001, 1002, 1003}}));
    CHECK(speed.rate() == PlaybackRate::X400);
    REQUIRE(speed.update({6, {2001, 1002, 1003}}));
    CHECK(speed.rate() == PlaybackRate::X200);
    REQUIRE(speed.update({4, {2001, 2002, 1003}}));
    CHECK(speed.rate() == PlaybackRate::X150);
    REQUIRE(speed.update({0, {2001, 2002, 2003}}));
    CHECK(speed.rate() == PlaybackRate::X125);
}

TEST_CASE("a replacement set or individual clothing slots use the same count policy") {
    const OutfitSpeedProfile replacement{"Other outfit", {"TestHead", nullptr, "TestLegs"},
        {PlaybackRate::Normal, PlaybackRate::X175, PlaybackRate::X200, PlaybackRate::X400}};
    OutfitSpeed speed(replacement);
    REQUIRE(speed.update({7, {1, 2, 3}}));
    CHECK(speed.mask() == 5);
    CHECK(speed.count() == 2);
    CHECK(speed.rate() == PlaybackRate::X200);
    REQUIRE(speed.update({2, {1, 2, 3}}));
    CHECK(speed.rate() == PlaybackRate::Normal);
    REQUIRE(speed.update({4, {1, 2, 3}}));
    CHECK(speed.rate() == PlaybackRate::X175);
}

TEST_CASE("world reset clears the previous outfit while identical rereads remain quiet") {
    OutfitSpeed speed;
    REQUIRE(speed.update({7, {1, 2, 3}}));
    for (unsigned frame = 0; frame < 120; ++frame) {
        CHECK_FALSE(speed.update({7, {1, 2, 3}}));
        CHECK(speed.rate() == PlaybackRate::X400);
    }
    speed.reset();
    CHECK(speed.rate() == PlaybackRate::X125);
    REQUIRE(speed.update({7, {1, 2, 3}}));
    CHECK(speed.rate() == PlaybackRate::X400);
    CHECK_FALSE(speed.update({7, {1, 2, 3}}));
}
