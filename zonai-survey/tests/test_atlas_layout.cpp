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
    CHECK(atlas::kBatchQuads*sizeof(atlas::Quad)==2048);
    CHECK(atlas::kMaxQuads==2048);
    CHECK(atlas::kSlotCount==3);
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
TEST_CASE("letters keep their pen offsets and land the baseline on whole pixels") {
    const auto grid=atlas::pixelGrid(1920,1080);
    CHECK(grid.x==doctest::Approx(1.5f)); CHECK(grid.y==doctest::Approx(1.5f));
    CHECK(atlas::pixelGrid(1600,900).x==doctest::Approx(1.25f));
    CHECK(atlas::pixelGrid(0,0).x==0);
    CHECK(atlas::pixelGrid(1600,9000).y==0);
    // A whole-pixel offset added to a snapped origin stays on the grid.
    const float origin=grid.snapX(311.37f), name=origin+grid.snapX(19);
    CHECK(std::fabs(name*grid.x-std::round(name*grid.x))<1e-3f);
    unsigned unsnappedFractions=0;
    for(int i=0;i<400;++i) {
        const float pen=100+i*0.37f, top=200+i*0.29f;
        const auto q=atlas::letterQuad(grid,pen,top,256,0xffffffffu);
        CHECK(atlas::quadSize(q.tile)==22);
        CHECK(q.tile==256); CHECK(atlas::tileSize(q.tile)==16);
        // labels.vert maps the cell at an equal margin inside the quad.
        const float margin=(atlas::quadSize(q.tile)-16)*0.5f;
        CHECK(margin==atlas::kLetterMargin);
        const float basePixel=(q.y+margin+atlas::kCellBaseline)*grid.y;
        CHECK(std::fabs(basePixel-std::round(basePixel))<1e-3f);
        // Letters move with the word: the pen is never snapped on its own.
        CHECK(q.x+margin==doctest::Approx(pen));
        CHECK(std::fabs(q.y+margin-top)<=0.5f/grid.y+1e-4f);
        const float raw=(top+atlas::kCellBaseline)*grid.y;
        unsnappedFractions+=std::fabs(raw-std::round(raw))>0.05f;
    }
    // The sampled baselines really start off-grid.
    CHECK(unsnappedFractions>100);
    const auto loose=atlas::letterQuad(atlas::PixelGrid{},10.3f,20.7f,0,0);
    CHECK(loose.x==doctest::Approx(7.3f)); CHECK(loose.y==doctest::Approx(17.7f));
}
TEST_CASE("compact quads reconstruct original icon and letter vertices without quantization") {
    constexpr unsigned corners[][2]{{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
    for(unsigned icon=0;icon<2;++icon) for(unsigned cell=0;cell<(icon?29u:96u);++cell) {
        const unsigned offset=icon?atlas::iconOffset(cell):cell*256;
        const float size=icon?16.f:22.f, dimensions=icon?32.f:16.f;
        for(unsigned sample=0;sample<128;++sample) {
            const float x=-3.25f+sample*10.125f, y=0.33f+sample*5.25f;
            const atlas::Quad q{x,y,offset|(icon?atlas::kIconTile:0),0x9f1e3a7bu};
            CHECK((q.tile&65535u)==offset);
            CHECK(q.color==0x9f1e3a7bu);
            for(const auto& corner:corners) for(unsigned axis=0;axis<2;++axis) {
                const float pos=axis?q.y:q.x, uv=float(corner[axis]);
                CHECK(pos+uv*atlas::quadSize(q.tile)==pos+uv*size);
                CHECK((uv*atlas::quadSize(q.tile)-(atlas::quadSize(q.tile)-16)*0.5f)*atlas::tileSize(q.tile)*0.0625f-0.5f
                    ==(uv*size-(size-16)*0.5f)*dimensions*0.0625f-0.5f);
            }
        }
    }
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
