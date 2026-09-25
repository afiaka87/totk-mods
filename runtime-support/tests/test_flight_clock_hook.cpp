// SPDX-License-Identifier: MIT
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include "FlightClockHook.hpp"
#include "../pure/FlightClock.hpp"
#include <array>
using namespace arrowbound;
TEST_CASE("Unpublished clock rejects following; a published valid clock admits it") {
    pure::FlightMailbox<pure::FlightTime> mailbox;
    pure::FlightTime time{};
    pure::FlightClockCursor cursor;
    float elapsed{};
    REQUIRE(mailbox.snapshot(time));
    CHECK_FALSE(cursor.step(time,elapsed));
    pure::FlightClock clock;
    REQUIRE(mailbox.publish(clock.update(1,false,true)));
    REQUIRE(mailbox.snapshot(time));
    CHECK(cursor.step(time,elapsed));
    REQUIRE(mailbox.publish(clock.update(1,false,true)));
    REQUIRE(mailbox.snapshot(time));
    CHECK(cursor.step(time,elapsed));
    CHECK(elapsed==doctest::Approx(1.0/30.0));
}
namespace {
game_clock::FrameCallback beforeArrow{},beforeRecall{};
pure::FlightClock arrowClock,recallClock;
unsigned nativeCalls{};
void* module{}; const void* context{}; bool paused{};
void game(void* a,const void* b,float scale) {
    CHECK(a==module); CHECK(b==context); CHECK(scale==0.5f); ++nativeCalls;
}
void arrow(void* a,const void* b,float scale) { beforeArrow(a,b,scale); arrowClock.update(scale,paused,true); }
void recall(void* a,const void* b,float scale) { beforeRecall(a,b,scale); recallClock.update(scale,paused,true); }
}
TEST_CASE("Timing callback keeps both clocks and native game advancing exactly once in either order") {
    for(bool recallFirst:{false,true}) {
        alignas(8) std::array<std::uint32_t,5> words{0xf9400828};
        const auto site=reinterpret_cast<std::uintptr_t>(words.data());
        beforeArrow=beforeRecall=nullptr; arrowClock={}; recallClock={}; nativeCalls=0;
        queryResult=0; queryPermission=Perm_Rx; exl::hook::original=reinterpret_cast<std::uintptr_t>(game);
        module=&words[0]; context=&words[1];
        if(recallFirst) beforeRecall=exl::hook::Hook(site,recall,true);
        REQUIRE(game_clock::installClockHook(site,arrow,beforeArrow));
        if(!recallFirst) beforeRecall=exl::hook::Hook(site,recall,true);
        const auto top=totk::render::decodePfxEntry(site,words.data()).previous;
        for(int i=0;i<3;++i) {
            paused=i==1;
            reinterpret_cast<game_clock::FrameCallback>(top)(module,context,0.5f);
        }
        CHECK(nativeCalls==3);
        auto a=arrowClock.update(0,true,true),b=recallClock.update(0,true,true);
        CHECK(a.serial==4); CHECK(b.serial==4); CHECK(a.nanoseconds==33333333);
        CHECK(a.nanoseconds==b.nanoseconds);
    }
}
TEST_CASE("Unknown or non-executable clock patches remain refused") {
    for(bool badPermission:{false,true}) {
        alignas(8) std::array<std::uint32_t,5> words{0xd503201f};
        const auto site=reinterpret_cast<std::uintptr_t>(words.data());
        beforeArrow=nullptr; queryResult=0; queryPermission=1;
        if(badPermission) exl::hook::Hook(site,recall,false);
        const auto original=words;
        CHECK_FALSE(game_clock::installClockHook(site,arrow,beforeArrow));
        CHECK(words==original); CHECK(beforeArrow==nullptr);
    }
}
