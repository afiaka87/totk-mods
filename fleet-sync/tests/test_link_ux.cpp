#include <doctest.h>
#include <cstring>
#include <initializer_list>
#include "pure/LinkInput.hpp"
#include "pure/LinkFeedback.hpp"

using linked_stick::pure::LinkAction;
using linked_stick::pure::LinkInput;
namespace {
constexpr auto chord = LinkInput::clicks;
constexpr std::uint64_t l3=1ull<<4, r3=1ull<<5, l=1ull<<6, r=1ull<<7;
constexpr std::uint64_t zl=1ull<<8, zr=1ull<<9, plus=1ull<<10, minus=1ull<<11;
constexpr std::uint64_t left=1ull<<12, up=1ull<<13, right=1ull<<14, down=1ull<<15;
}

TEST_CASE("quick linking chooses controller first then receivers and fires once on release") {
    for (bool haveController : {false,true}) {
        LinkInput input;
        CHECK(input.step(chord,true,haveController,0)==LinkAction::None);
        CHECK(input.ownedButtons()==chord);
        CHECK(input.step(chord,true,haveController,100)==LinkAction::None);
        CHECK(input.step(0,true,haveController,150)==
              (haveController?LinkAction::Receiver:LinkAction::Controller));
        CHECK(input.step(0,true,haveController,160)==LinkAction::None);
    }
}

TEST_CASE("both click orders and staggered releases work without leaking the held click") {
    for (const auto first : {l3,r3}) {
        LinkInput input;
        CHECK(input.step(first,true,false,0)==LinkAction::None);
        CHECK(input.ownedButtons()==0);
        input.step(chord,true,false,20);
        CHECK(input.step(first,true,false,100)==LinkAction::None);
        CHECK(input.ownedButtons()==first);
        CHECK(input.step(0,true,false,120)==LinkAction::Controller);
        CHECK(input.ownedButtons()==0);
    }
}

TEST_CASE("clear hold lasts 1500 milliseconds at all frame rates and never also pairs") {
    for (unsigned hz : {30u,60u,120u}) {
        LinkInput input;
        unsigned clearCount=0;
        for (unsigned i=0; i<=hz*2; ++i) {
            const unsigned ms=i*1000/hz;
            const auto action=input.step(chord,true,true,ms);
            CHECK((action==LinkAction::None || action==LinkAction::Clear));
            if (action==LinkAction::Clear) {
                CHECK(ms>=LinkInput::holdMs);
                CHECK(ms<LinkInput::holdMs+1000/hz+1);
                ++clearCount;
            }
        }
        CHECK(clearCount==1);
        CHECK(input.step(l3,true,false,2010)==LinkAction::None);
        CHECK(input.ownedButtons()==l3);
        CHECK(input.step(0,true,false,2020)==LinkAction::None);
        input.step(chord,true,false,2050);
        CHECK(input.step(0,true,false,2100)==LinkAction::Controller);
    }
}

TEST_CASE("published and UltraCam global chords neither pair nor get masked with added clicks") {
    const std::uint64_t reserved[]{zl|zr|l3,zl|r3,zl|l,zl|up,zl|left,zl|right,
        zl|down,zl|zr|left,zr|down,zr|plus,zl|minus,zl|2ull};
    for (const auto foreign : reserved) {
        for (const bool extraClicks : {false,true}) {
            LinkInput input;
            const auto held=foreign | (extraClicks?chord:0);
            for (unsigned ms=0; ms<=2000; ms+=100) {
                CHECK(input.step(held,true,true,ms)==LinkAction::None);
                CHECK(input.ownedButtons()==0);
            }
            CHECK(input.step(0,true,true,2010)==LinkAction::None);
        }
    }
}

TEST_CASE("foreign chords cancel armed gestures without consuming the foreign input") {
    for (auto foreign : {zl,zr,l,r,plus,minus}) {
        LinkInput input;
        input.step(chord,true,false,0);
        CHECK(input.step(chord|foreign,true,false,100)==LinkAction::None);
        CHECK(input.ownedButtons()==0);
        CHECK(input.step(chord,true,false,150)==LinkAction::None);
        CHECK(input.step(0,true,false,200)==LinkAction::None);
    }
}

TEST_CASE("context loss clock reversal and observation gaps cancel pending pairing") {
    for (bool gap : {false,true}) {
        LinkInput input;
        input.step(chord,true,false,0);
        CHECK(input.step(chord,gap,gap,gap?1000:20)==LinkAction::None);
        CHECK(input.step(0,true,false,gap?1010:30)==LinkAction::None);
    }
    LinkInput input;
    input.step(chord,false,false,0);
    input.step(chord,true,false,20);
    CHECK(input.step(0,true,false,40)==LinkAction::None);
    input.step(chord,true,false,500);
    CHECK(input.step(0,true,false,499)==LinkAction::None);
}

TEST_CASE("face and dpad extras cancel pairing instead of causing unintended selection") {
    for (const auto extra : {1ull,2ull,4ull,8ull,1ull<<12,1ull<<13,1ull<<14,1ull<<15}) {
        LinkInput input;
        input.step(chord,true,false,0);
        CHECK(input.step(chord|extra,true,false,50)==LinkAction::None);
        CHECK(input.step(0,true,false,100)==LinkAction::None);
    }
}

TEST_CASE("audio-only feedback distinguishes accepted roles reset refusal and no action") {
    using linked_stick::pure::selectionCue;
    for (bool accepted : {false,true}) CHECK(selectionCue(LinkAction::None,accepted)==nullptr);
    for (auto action : {LinkAction::Controller,LinkAction::Receiver,LinkAction::Clear})
        CHECK(std::strcmp(selectionCue(action,false),"mc_AmiiboError")==0);
    CHECK(std::strcmp(selectionCue(LinkAction::Controller,true),"MapMarker_1")==0);
    CHECK(std::strcmp(selectionCue(LinkAction::Receiver,true),"mc_HeartUp_Short")==0);
    CHECK(std::strcmp(selectionCue(LinkAction::Clear,true),"AmiiboMarker_Sign")==0);
}
