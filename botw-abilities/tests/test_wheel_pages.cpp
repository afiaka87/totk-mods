#include "doctest.h"
#include "WheelPages.hpp"
#include <ostream>
#include <string_view>
using namespace botw_abilities::wheel;

namespace {
constexpr std::uint64_t L = 1ull << 6, R = kButtonR;
constexpr unsigned kBoth = offer(Choice::Stasis) | offer(Choice::Cryonis);
constexpr std::int32_t kBottom = 4, kBottomLeft = 5;
} // namespace

TEST_CASE("A fresh R press while the wheel is open turns once and stays hidden until release") {
    PageInput input;
    CHECK_FALSE(input.step(L, true, true).turn);
    const auto press = input.step(L | R, true, true);
    CHECK(press.turn);
    CHECK(press.mask == R);
    const auto held = input.step(L | R, true, true);
    CHECK_FALSE(held.turn);
    CHECK(held.mask == R);
    // Still hidden after the wheel closes, until the finger leaves R.
    CHECK(input.step(R, true, false).mask == R);
    const auto released = input.step(0, true, false);
    CHECK_FALSE(released.turn);
    CHECK(released.mask == 0);
}

TEST_CASE("An R held before the wheel opened is left to the game") {
    PageInput input;
    CHECK(input.step(R, true, false).mask == 0);
    const auto opened = input.step(L | R, true, true);
    CHECK_FALSE(opened.turn);
    CHECK(opened.mask == 0);
}

TEST_CASE("Repeated reads of the same input sample do not turn twice") {
    PageInput input;
    CHECK(input.step(R, true, true).turn);
    const auto stale = input.step(R, false, true);
    CHECK_FALSE(stale.turn);
    CHECK(stale.mask == R);
    CHECK_FALSE(input.step(R, true, true).turn);
}

TEST_CASE("Taps cycle to the abilities page and back") {
    CHECK(turnPage(0, 1) == 1);
    CHECK(turnPage(1, 1) == 0);
    CHECK(turnPage(0, 2) == 0);
}

TEST_CASE("The abilities page shows Stasis at the bottom and Cryonis at the bottom left only") {
    const Wedge fuse{"UI/Tex/Icon/Obj_OneTouchBond.bntx", true, false};
    const auto native = plan(0, kBottom, fuse, kBoth);
    CHECK(std::string_view(native.path) == "UI/Tex/Icon/Obj_OneTouchBond.bntx");
    CHECK(native.valid);

    const auto stasis = plan(1, kBottom, fuse, kBoth);
    CHECK(std::string_view(stasis.path) == "UI/Tex/Icon/BotwStasis.bntx");
    CHECK(stasis.valid);
    CHECK_FALSE(stasis.check);
    const auto cryonis = plan(1, kBottomLeft, fuse, kBoth);
    CHECK(std::string_view(cryonis.path) == "UI/Tex/Icon/BotwCryonis.bntx");
    CHECK(cryonis.valid);
    for (std::size_t wedge = 0; wedge < kWedges; ++wedge) {
        if (wedge == kBottom || wedge == kBottomLeft)
            continue;
        const auto empty = plan(1, wedge, fuse, kBoth);
        CHECK(empty.path == nullptr);
        CHECK_FALSE(empty.valid);
    }
}

TEST_CASE("An ability whose mod did not install leaves its wedge empty") {
    const auto stasis = plan(1, kBottom, {}, offer(Choice::Cryonis));
    CHECK(stasis.path == nullptr);
    CHECK_FALSE(stasis.valid);
    CHECK(plan(1, kBottomLeft, {}, offer(Choice::Cryonis)).valid);
    CHECK(exitPage(1, kBottom, false, offer(Choice::Cryonis)).choice == Choice::None);
}
TEST_CASE("Both remote bomb wedges choose their own shape and respect cancellation") {
    const auto offers = kBoth | offer(Choice::Sphere) | offer(Choice::Cube);
    CHECK(exitPage(1,0,false,offers).choice == Choice::Sphere);
    CHECK(exitPage(1,1,false,offers).choice == Choice::Cube);
    CHECK(exitPage(1,0,false,offers).cancel);
    CHECK(exitPage(1,1,true,offers).choice == Choice::None);
    CHECK(exitPage(1,1,false,offer(Choice::Sphere)).choice == Choice::None);
    CHECK(std::string_view(plan(1,0,{},offers).path)=="UI/Tex/Icon/BotwBomb.bntx");
    CHECK(std::string_view(plan(1,1,{},offers).path)=="UI/Tex/Icon/BotwBombCube.bntx");
}

TEST_CASE("Releasing L on the abilities page picks the wedge and always closes as cancelled") {
    const auto stasis = exitPage(1, kBottom, false, kBoth);
    CHECK(stasis.cancel);
    CHECK(stasis.choice == Choice::Stasis);
    const auto cryonis = exitPage(1, kBottomLeft, false, kBoth);
    CHECK(cryonis.cancel);
    CHECK(cryonis.choice == Choice::Cryonis);
    // An empty wedge, a B cancel or an odd cursor picks nothing but still keeps the native ability.
    for (const auto exit : {exitPage(1, 0, false, kBoth), exitPage(1, kBottom, true, kBoth),
                            exitPage(1, -1, false, kBoth), exitPage(1, 8, false, kBoth)}) {
        CHECK(exit.cancel);
        CHECK(exit.choice == Choice::None);
    }
    const auto native = exitPage(0, kBottom, false, kBoth);
    CHECK_FALSE(native.cancel);
    CHECK(native.choice == Choice::None);
}

TEST_CASE("Every page icon path fits the copy buffer and names an icon file") {
    for (const auto &entry : kAbilityPage) {
        if (!entry.path) {
            CHECK(textLength(entry.caption) == 0);
            CHECK(entry.choice == Choice::None);
            continue;
        }
        const std::string_view path(entry.path);
        CHECK(path.size() < kPathCapacity);
        CHECK(path.starts_with("UI/Tex/Icon/"));
        CHECK(path.ends_with(".bntx"));
        CHECK(textLength(entry.caption) > 0);
        CHECK(entry.choice != Choice::None);
    }
}
