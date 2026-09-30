// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <initializer_list>
#include <limits>
#include "TravelGlidePose.hpp"
#include "TravelFacing.hpp"
using namespace zonai_hookshot::pure;

namespace {
PositionZipState path(float total=100.f,float advanced=50.f) {
    PositionZipState p{};p.active=true;p.travelDistance=total;p.advanced=advanced;p.direction={0,0,1};return p;
}
TravelGlideBlend sample(const PositionZipState& p,Vec3 forward={0,0,1}) {
    return travelGlideBlend(Phase::PositionCruise,true,false,p,forward);
}
}
TEST_CASE("travel glide reaches full native strength with bounded transitions") {
    for(float distance:{2.f,12.f,60.f,240.f}) {
        CHECK(sample(path(distance,0)).weight==0);
        CHECK(sample(path(distance,distance/2)).weight==doctest::Approx(1));
        CHECK(sample(path(distance,distance)).weight==0);
        const float ramp=std::fmin(6.f,distance*0.15f);
        CHECK(sample(path(distance,ramp/2)).weight==doctest::Approx(0.5f));
        CHECK(sample(path(distance,distance-ramp/2)).weight==doctest::Approx(0.5f));
    }
    CHECK(glideAnimationValue(3,3,7,0.f,sample(path()))==1.f);
    CHECK(glideAnimationValue(3,3,7,0.7f,sample(path()))==doctest::Approx(1.f));
    CHECK(glideAnimationValue(12,3,7,0.23f,sample(path()))==0.23f);
}
TEST_CASE("travel glide direction follows travel relative to Link in native degrees") {
    auto p=path();
    CHECK(sample(p).directionDegrees==0);
    p.direction={1,0,0};CHECK(sample(p).directionDegrees==doctest::Approx(90));
    p.direction={-1,0,0};CHECK(sample(p).directionDegrees==doctest::Approx(-90));
    p.direction={0,0,-1};CHECK(std::fabs(sample(p).directionDegrees)==doctest::Approx(180));
    p.direction={0,1,0};CHECK(sample(p).directionDegrees==0);
    CHECK(sample(p).weight==1);
    CHECK(std::fabs(glideAnimationValue(7,3,7,170.f,{0.5f,-170.f}))==doctest::Approx(180));
    CHECK(glideAnimationValue(7,3,7,-170.f,{1.f,170.f})==doctest::Approx(170));
}
TEST_CASE("travel glide is released outside travel and for invalid or finished paths") {
    for(unsigned phase=0;phase<=unsigned(Phase::Cooldown);++phase) {
        if(Phase(phase)==Phase::PositionCruise)continue;
        CHECK(travelGlideBlend(Phase(phase),true,false,path(),{0,0,1}).weight==0);
    }
    CHECK(travelGlideBlend(Phase::PositionCruise,false,false,path(),{0,0,1}).weight==0);
    CHECK(travelGlideBlend(Phase::PositionCruise,true,true,path(),{0,0,1}).weight==0);
    auto p=path();p.active=false;CHECK(sample(p).weight==0);
    p=path(0,0);CHECK(sample(p).weight==0);
    p=path(50,60);CHECK(sample(p).weight==0);
    p=path(50,-1);CHECK(sample(p).weight==0);
    p=path();p.advanced=std::numeric_limits<float>::quiet_NaN();CHECK(sample(p).weight==0);
    CHECK(sample(path(),{0,0,0}).weight==0);
    CHECK(glideAnimationValue(3,3,7,0.23f,{})==0.23f);
    CHECK(glideAnimationValue(7,3,7,-34.f,{})==-34.f);
}
TEST_CASE("glide pose remains aligned with the accepted gradual wall turn") {
    PositionZipState p{};
    REQUIRE(beginPositionZip(p,{0,0,0},{0,0,101}));
    float basis[9]{1,0,0,0,1,0,0,0,1};YawState yaw{};
    REQUIRE(prepareTravelFacing(yaw,basis,p.direction,{1,0,0},p.travelDistance));
    for(float advanced:{10.f,45.f,80.f,90.f,97.f}) {
        p.advanced=advanced;updateTravelFacing(yaw,advanced);
        float live[9]{};REQUIRE(rotateBasisWorldYaw(basis,yaw.signedRadians*yawEasedProgress(yaw),live));
        const auto blend=sample(p,{live[2],0,live[8]});
        CHECK(blend.directionDegrees==doctest::Approx(-yaw.signedRadians*yawEasedProgress(yaw)*57.2957795f));
        CHECK(blend.weight>0);
    }
    p.advanced=p.travelDistance;CHECK(sample(p).weight==0);
    CHECK(yaw.finished);
}
