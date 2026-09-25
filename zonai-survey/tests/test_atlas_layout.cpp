// SPDX-License-Identifier: MIT
#include <doctest.h>
#include "AtlasLayout.hpp"
#if SURVEY_TEST_GAME_DATA
#include "GlyphIndex.hpp"
#endif
#include <thread>
#include <limits>

using namespace zonai_survey;
TEST_CASE("atlas cells and drawing slots stay within allocation bounds") {
    constexpr auto bytes=96*256+29*1024;
    CHECK(bytes==54272);
    for(unsigned c=32;c<=126;++c) CHECK(atlas::asciiOffset(c)+256<=96*256);
    CHECK(atlas::asciiOffset(0x2019)==95*256);
    for(unsigned i=0;i<29;++i) CHECK(atlas::iconOffset(i)+1024<=bytes);
    CHECK(atlas::iconOffset(255)==atlas::iconOffset(0));
    CHECK(atlas::asciiOffset(0xffff)==atlas::asciiOffset('?'));
    CHECK(atlas::kBatchQuads*sizeof(atlas::Quad)==4096);
    CHECK(atlas::kSlotBytes%(atlas::kBatchQuads*sizeof(atlas::Quad))==0);
}
#if SURVEY_TEST_GAME_DATA
TEST_CASE("atlas covers every locally generated collectible label") {
    unsigned longest=0,extras=0;
    for(unsigned i=0;i<glyphs::kNameCount;++i) {
        const char* p=pure::glyphDisplayName(i);
        unsigned length=0;
        while(p && *p) {
            const auto c=atlas::nextCharacter(p);
            CHECK(((c>=32 && c<=126) || c==0x2019));
            extras+=c==0x2019; ++length;
        }
        if(length>longest) longest=length;
    }
    CHECK(longest==37);
    CHECK(extras>0);
    CHECK(48*(longest+1)+190<=atlas::kMaxQuads);
}
#endif
TEST_CASE("atlas projection follows camera rotation and rejects invalid points") {
    float view[]{1,0,0,0, 0,1,0,0, 0,0,1,0};
    float proj[]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-1,0};
    float x{},y{};
    REQUIRE(atlas::project(view,proj,0,0,-10,x,y)); CHECK(x==640); CHECK(y==360);
    REQUIRE(atlas::project(view,proj,5,5,-10,x,y)); CHECK(x==960); CHECK(y==180);
    float rotated[]{0,0,-1,0, 0,1,0,0, 1,0,0,0};
    REQUIRE(atlas::project(rotated,proj,-10,0,0,x,y)); CHECK(x==640); CHECK(y==360);
    CHECK_FALSE(atlas::project(view,proj,0,0,10,x,y));
    CHECK_FALSE(atlas::project(view,proj,11,0,-10,x,y));
    CHECK_FALSE(atlas::project(view,proj,std::numeric_limits<float>::quiet_NaN(),0,-10,x,y));
    CHECK(atlas::rgba(1,0.5f,0,1)==0xff0080ff);
}
TEST_CASE("atlas mailbox never writes a snapshot being read") {
    struct Frame { unsigned sequence{},values[64]{}; };
    atlas::Mailbox<Frame> box;
    CHECK(box.consume().sequence==0);
    std::atomic<bool> done{};
    std::thread producer([&] {
        for(unsigned n=1;n<=100000;++n) {
            Frame f; f.sequence=n; for(auto& v:f.values) v=n;
            box.publish(f);
        }
        done.store(true,std::memory_order_release);
    });
    bool coherent=true; unsigned previous=0;
    do {
        const auto& f=box.consume();
        coherent &= f.sequence>=previous; previous=f.sequence;
        for(const auto v:f.values) coherent &= v==f.sequence;
    } while(!done.load(std::memory_order_acquire));
    producer.join();
    CHECK(coherent);
    CHECK(box.consume().sequence==100000);
    box.publish(Frame{});
    CHECK(box.consume().sequence==0);
}
