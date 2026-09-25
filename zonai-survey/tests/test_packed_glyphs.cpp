// SPDX-License-Identifier: MIT
#include <doctest.h>
#include <vector>
#include "GlyphIndex.hpp"
namespace sg = zonai_survey::glyphs;
namespace sp = zonai_survey::pure;
TEST_CASE("packed map preserves every collectible coordinate name and flag exactly") {
    std::uint32_t seen=0;
    for (unsigned cell=0;cell<sg::kGridWidth*sg::kGridHeight;++cell) {
        for (unsigned i=sg::kCellStart[cell];i<sg::kCellStart[cell+1];++i) {
            const auto p=sg::unpackPlacement(i,cell);
            const auto& old=sg::kPlacements[i];
            CHECK(p.x==old.x); CHECK(p.y==old.y); CHECK(p.z==old.z);
            CHECK(p.name==old.name); CHECK(p.flags==old.flags); ++seen;
        }
    }
    CHECK(seen==sg::kPlacementCount);
    CHECK(sizeof(sg::kPlacementBytes)<sizeof(sg::kPlacements)*.63);
}
TEST_CASE("packed nearby queries preserve original order coverage and distance at cell edges") {
    for (float x : {-5120.f,-256.f,-.1f,0.f,255.9f,256.f,5119.f})
        for (float z : {-4096.f,-256.f,-.1f,0.f,255.9f,4095.f})
            for (float radius : {0.f,1.f,30.f,256.f,450.f}) {
                std::vector<sg::Placement> expected,actual;
                std::vector<float> oldDistances,newDistances;
                sp::forEachPlacementNear<false>(x,z,radius,[&](auto p,float d){expected.push_back(p);oldDistances.push_back(d);});
                sp::forEachPlacementNear<true>(x,z,radius,[&](auto p,float d){actual.push_back(p);newDistances.push_back(d);});
                REQUIRE(expected.size()==actual.size());
                CHECK(oldDistances==newDistances);
                for (unsigned i=0;i<expected.size();++i) {
                    CHECK(expected[i].x==actual[i].x); CHECK(expected[i].y==actual[i].y);
                    CHECK(expected[i].z==actual[i].z); CHECK(expected[i].name==actual[i].name);
                    CHECK(expected[i].flags==actual[i].flags);
                }
            }
}
