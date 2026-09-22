// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <algorithm>
#include <limits>
#include "../src/pure/RagdollTransport.hpp"
using namespace arrowbound::pure;
namespace {
RagdollBodyPose pose(Vec3 position, Vec3 velocity={}) {
    return {{{0,0,1,position.x, 0,1,0,position.y, -1,0,0,position.z}},velocity};
}
void same(Vec3 a, Vec3 b) {
    CHECK(a.x==doctest::Approx(b.x).epsilon(0.0001).scale(1));
    CHECK(a.y==doctest::Approx(b.y).epsilon(0.0001).scale(1));
    CHECK(a.z==doctest::Approx(b.z).epsilon(0.0001).scale(1));
}
}
TEST_CASE("ordinary bounded ragdoll offsets do not enable speed exception") {
    RagdollTransport transport;
    for (float gap:{0.0f,1.6f,4.22f,7.99f}) {
        std::array bodies{pose({100,20,30},{132,0,0}),pose({100,19,30})};
        const auto original=bodies;
        CHECK(transport.prepare(bodies,0,{100+gap,20,30})==TransportResult::Native);
        CHECK_FALSE(transport.engaged);
        for (unsigned i=0;i<bodies.size();++i) {
            CHECK(bodies[i].matrix==original[i].matrix);
            same(bodies[i].velocity,original[i].velocity);
        }
    }
}
TEST_CASE("fast ragdoll gate never teleports or rewrites pose velocity") {
    RagdollTransport transport;
    std::array bodies{pose({1500,300,-500},{90,1,-2}),pose({1500.4f,299,-500.2f},{92,0,-3})};
    const auto original=bodies;
    CHECK(transport.prepare(bodies,0,{1700,345,-650})==TransportResult::Fast);
    CHECK(transport.engaged);
    for (unsigned i=0;i<bodies.size();++i) {
        CHECK(bodies[i].matrix==original[i].matrix);
        same(bodies[i].velocity,original[i].velocity);
    }
    CHECK(transport.prepare(bodies,0,bodies[0].position())==TransportResult::Fast);
    transport={};
    CHECK(transport.prepare(bodies,0,bodies[0].position())==TransportResult::Native);
}
TEST_CASE("native velocity following catches up without position writes at low or steep pitch") {
    // 90 is clean RagdollPlayer.MaxLinearSpeed; 237 is observed normalized Bow of Light speed.
    for (Vec3 direction:{Vec3{1,0,0},Vec3{0.6f,0.8f,0},Vec3{0,-1,0}}) {
        RagdollTransport transport;
        Vec3 target{},limited{},unlimited{};
        for (unsigned frame=0;frame<180;++frame) {
            const float dt=(frame%3==0)?1.0f/60:1.0f/30;
            target=add(target,mul(direction,237*dt));
            auto oldDesired=mul(sub(target,limited),1/dt);
            limited=add(limited,mul(oldDesired,90*dt/length(oldDesired)));
            std::array bodies{pose(unlimited)};
            const auto original=bodies;
            const auto result=transport.prepare(bodies,0,target);
            const auto desired=mul(sub(target,unlimited),1/dt);
            const auto command=result==TransportResult::Fast ? boundedFlightVelocity(desired)
                : mul(desired,std::min(1.0f,90/length(desired)));
            unlimited=add(unlimited,mul(command,dt));
            CHECK(bodies[0].matrix==original[0].matrix);
            CHECK(dot(command,direction)>0);
            if (transport.engaged) same(unlimited,target);
        }
        CHECK(distance(limited,target)>500);
        CHECK(distance(unlimited,target)<0.01f);
    }
}
TEST_CASE("speed lease restores exact original after cancellation and repeated updates") {
    SpeedLimitLease lease;
    CHECK(lease.update(123,90,true)==5000);
    CHECK(lease.owned);
    for (unsigned i=0;i<100;++i) CHECK(lease.update(123,5000,true)==5000);
    CHECK(lease.original==90);
    CHECK(lease.update(123,5000,false)==90);
    CHECK_FALSE(lease.owned);
    CHECK(lease.update(123,90,false)==90);
}
TEST_CASE("lease does not undo a native replacement or a reused motion address") {
    SpeedLimitLease lease;
    lease.update(123,90,true);
    CHECK(lease.update(123,120,false)==120);
    CHECK_FALSE(lease.owned);
    lease.update(123,120,true);
    CHECK(lease.update(456,300,false)==300);
    CHECK_FALSE(lease.owned);
    CHECK(lease.update(456,300,true)==5000);
    CHECK(lease.update(456,5000,false)==300);
}
TEST_CASE("native limit changes during ownership become the restored baseline") {
    SpeedLimitLease lease;
    lease.update(123,90,true);
    CHECK(lease.update(123,150,true)==5000);
    CHECK(lease.update(123,5000,false)==150);
    CHECK(lease.update(123,6000,true)==6000);
    CHECK_FALSE(lease.owned);
    CHECK(lease.update(123,6000,false)==6000);
}
TEST_CASE("flight velocity remains bounded and keeps native direction") {
    same(boundedFlightVelocity({237,0,0}),{237,0,0});
    same(boundedFlightVelocity({}),{});
    same(boundedFlightVelocity({6000,8000,0}),{3000,4000,0});
    same(boundedFlightVelocity({-6000,-8000,0}),{-3000,-4000,0});
}
TEST_CASE("invalid poses cannot engage fast transport") {
    for (unsigned fault=0;fault<5;++fault) {
        RagdollTransport transport;
        std::array bodies{pose({0,0,0}),pose({0,-1,0})};
        Vec3 target{200,0,0};
        unsigned root=0;
        if (fault==0) root=2;
        if (fault==1) target.x=NAN;
        if (fault==2) bodies[1].velocity.x=INFINITY;
        if (fault==3) bodies[1].matrix[0]=NAN;
        if (fault==4) target.x=std::numeric_limits<float>::max();
        const auto first=bodies[0];
        CHECK(transport.prepare(bodies,root,target)==TransportResult::Invalid);
        CHECK_FALSE(transport.engaged);
        CHECK(bodies[0].matrix==first.matrix);
    }
}
TEST_CASE("mapped root uses native row-major basis") {
    const auto body=pose({100,20,30});
    same(transformPoint(body.matrix.data(),{1,2,3}),{103,22,29});
}
