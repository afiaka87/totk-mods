// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <algorithm>
#include "../src/pure/FlightClock.hpp"
#include "../src/pure/ModelTrace.hpp"
#include "ArrowHookshot.hpp"
using namespace arrowbound::pure;

TEST_CASE("native clock handles fractional cadence pause and repeated input callbacks") {
    for (const auto scale : {0.25f,0.5f,0.75f,1.0f,1.5f}) {
        FlightClock clock;
        FlightClockCursor cursor;
        float delta{};
        auto value = clock.update(scale,false,true);
        REQUIRE(cursor.step(value,delta));
        CHECK(delta == 0);
        for (int i=0;i<120;++i) {
            value = clock.update(scale,false,true);
            REQUIRE(cursor.step(value,delta));
            CHECK(delta == doctest::Approx(scale/30).epsilon(0.00001));
            REQUIRE(cursor.step(value,delta));
            CHECK(delta == 0);
        }
        const auto before = value.nanoseconds;
        for (int i=0;i<100;++i) {
            value = clock.update(1.5f,true,true);
            REQUIRE(cursor.step(value,delta));
            CHECK(delta == 0);
            CHECK(value.nanoseconds == before);
        }
        REQUIRE(cursor.step(clock.update(scale,false,true),delta));
        CHECK(delta == doctest::Approx(scale/30).epsilon(0.00001));
    }
    FlightClock clock;
    CHECK(clock.update(NAN,false,true).status == ClockStatus::Invalid);
    CHECK(clock.update(-1,false,true).status == ClockStatus::Invalid);
    CHECK(clock.update(1,false,false).status == ClockStatus::Unavailable);
    CHECK(clock.update(0,false,true).status == ClockStatus::Paused);
}
TEST_CASE("simulation time keeps fast level and pitched followers aligned under larger steps") {
    for (const auto velocity : {Vec3{120,0,0},Vec3{230,40,10},Vec3{10,230,-60}}) {
        ArrowFollower follower, fixed;
        FlightClock clock;
        FlightClockCursor cursor;
        Vec3 result{}, old{}, direction{}, sample{};
        float seconds{}, time=0, oldMax=0;
        REQUIRE(cursor.step(clock.update(0.5f,false,true),seconds));
        REQUIRE(follower.update(0,sample,velocity,true,result,direction,{},seconds));
        REQUIRE(fixed.update(0,sample,velocity,true,old,direction));
        for (std::uint64_t i=1;i<=240;++i) {
            const float scale = i<30 || i>200 ? 0.5f : 0.75f;
            REQUIRE(cursor.step(clock.update(scale,false,true),seconds));
            time += seconds;
            const bool fresh = i%2 == 0;
            if (fresh) sample=mul(velocity,time);
            REQUIRE(follower.update(i,sample,velocity,fresh,result,direction,{},seconds));
            REQUIRE(fixed.update(i,sample,velocity,fresh,old,direction));
            CHECK(distance(result,mul(velocity,time))<0.004f);
            oldMax=std::max(oldMax,distance(old,mul(velocity,time)));
        }
        CHECK(oldMax>20); // Same native path with the old clock actually fails.
    }
}
TEST_CASE("missing callbacks consume elapsed time once and zero time never moves the carrier") {
    FlightClock clock;
    FlightClockCursor cursor;
    float delta{};
    REQUIRE(cursor.step(clock.update(0.5f,false,true),delta));
    (void)clock.update(0.5f,false,true);
    REQUIRE(cursor.step(clock.update(0.5f,false,true),delta));
    CHECK(delta == doctest::Approx(1.f/30));
    ArrowFollower follower;
    Vec3 p{},v{};
    REQUIRE(follower.update(4,{}, {120,0,0},true,p,v,{},0));
    REQUIRE(follower.update(6,{4,0,0}, {120,0,0},true,p,v,{},delta));
    CHECK(p.x == doctest::Approx(4));
    REQUIRE(follower.update(7,{4,0,0}, {120,0,0},false,p,v,{},0));
    CHECK(p.x == doctest::Approx(4));
    CHECK_FALSE(follower.update(8,{}, {120,0,0},true,p,v,{},NAN));
}
TEST_CASE("model completion waits for both lanes and excludes stale or foreign queues") {
    ModelCompletionJoin<2> join;
    join.beginFrame(1);
    REQUIRE(join.registerScene(10,1));
    CHECK(join.complete(10,1,1)==ModelJoinStatus::Waiting);
    CHECK(join.complete(10,1,1)==ModelJoinStatus::Waiting);
    CHECK(join.complete(10,1,2)==ModelJoinStatus::Complete);
    CHECK(join.complete(10,1,2)==ModelJoinStatus::Waiting);
    CHECK(join.complete(20,1,2)==ModelJoinStatus::UnknownScene);
    join.beginFrame(2);
    CHECK(join.complete(10,1,2)==ModelJoinStatus::WrongEpoch);
    REQUIRE(join.registerScene(20,2));
    CHECK(join.complete(20,2,2)==ModelJoinStatus::Waiting);
    CHECK(join.complete(20,2,1)==ModelJoinStatus::Complete);
}
TEST_CASE("missing arrow updates have time and distance budgets independent of input cadence") {
    for (const float dt : {1.f/120,1.f/60,1.f/30,0.05f}) {
        ArrowPredictionBudget budget;
        const Vec3 fast{237,0,0};
        REQUIRE(budget.step(true,fast,dt));
        float moved = 0;
        bool released = false;
        for (int i=0;i<30;++i) {
            if (!budget.step(false,fast,dt)) { released=true; break; }
            moved += 237*dt;
            CHECK(budget.step(false,fast,0)); // Duplicate/paused clock cannot consume budget.
        }
        CHECK(released);
        CHECK(moved <= ArrowPredictionBudget::kMaxDistance);
        CHECK(budget.step(true,fast,dt)); // A new shot/sample does not inherit stale time.
        CHECK(budget.seconds()==0);
        CHECK(budget.distance()==0);
    }
    ArrowPredictionBudget slow;
    REQUIRE(slow.step(true,{1,0,0},0));
    CHECK(slow.step(false,{1,0,0},0.05f));
    CHECK_FALSE(slow.step(false,{1,0,0},0.05f)); // Time, not distance, limits slow arrows.
    ArrowPredictionBudget gap;
    CHECK_FALSE(gap.step(false,{237,0,0},0.3f));
    CHECK_FALSE(gap.step(true,{NAN,0,0},0));
    CHECK_FALSE(gap.step(true,{1,0,0},NAN));
    CHECK_FALSE(gap.step(true,{1,0,0},-1));
    ArrowPredictionBudget alternating;
    for (int i=0;i<120;++i) CHECK(alternating.step(i%2==0,{237,0,0},0.05f));
}
TEST_CASE("completed culling snapshots distinguish rejection stale bounds and missing bounds") {
    std::array<std::byte,0x360> unit{};
    std::array<std::byte,0x30> render{};
    std::array<std::byte,0xA0> group{};
    std::array<std::byte,0x90> views{};
    float sphere[]{10,20,30,2};
    const auto write=[]<class T>(void* p,std::size_t at,T value) {
        std::memcpy(static_cast<char*>(p)+at,&value,sizeof(value));
    };
    write(unit.data(),0xC,std::uint32_t{5});
    write(unit.data(),0x24,std::uint16_t{2});
    write(unit.data(),0x30,static_cast<const void*>(render.data()));
    write(unit.data(),0x38,static_cast<const void*>(views.data()));
    write(unit.data(),0x58,static_cast<const void*>(sphere));
    write(render.data(),8,std::uint8_t{2});
    write(render.data(),0xC,std::uint32_t{4});
    auto c = inspectModelCull(unit.data());
    CHECK(c.mask==5); CHECK(c.shapes==2); CHECK(c.viewCount==2); CHECK(c.renderFlags==4);
    CHECK(c.views==views.data()); CHECK(c.spherePresent); CHECK(c.sphereValid); CHECK_FALSE(c.grouped);
    CHECK(distance(c.center,{10,20,30})==0); CHECK(c.radius==2);
    write(unit.data(),0xC,std::uint32_t{0});
    c=inspectModelCull(unit.data()); CHECK(c.mask==0); CHECK(c.sphereValid);
    CHECK(distance(c.center,{100,20,30})==90); // Valid bounds need not follow the actor.
    write(unit.data(),0x40,static_cast<const void*>(group.data()));
    write(group.data(),0x8C,Vec3{40,50,60}); write(group.data(),0x98,3.f);
    c=inspectModelCull(unit.data()); CHECK(c.grouped); CHECK(c.sphereValid);
    CHECK(distance(c.center,{40,50,60})==0); CHECK(c.radius==3);
    write(group.data(),0x98,-1.f); CHECK_FALSE(inspectModelCull(unit.data()).sphereValid);
    write(unit.data(),0x40,static_cast<const void*>(nullptr));
    sphere[0]=NAN; CHECK_FALSE(inspectModelCull(unit.data()).sphereValid);
    write(unit.data(),0x58,static_cast<const void*>(nullptr));
    c=inspectModelCull(unit.data()); CHECK_FALSE(c.spherePresent); CHECK_FALSE(c.sphereValid);
}
TEST_CASE("model admission distinguishes a present hidden node from an absent model") {
    std::array<std::byte,0x70> queue{};
    std::array<std::byte,0x28> node{};
    const void* groups[]{node.data()};
    const auto write=[]<class T>(void* p,std::size_t at,T value) {
        std::memcpy(static_cast<char*>(p)+at,&value,sizeof(value));
    };
    write(queue.data(),0x58,std::uint32_t{1});
    write(queue.data(),0x60,static_cast<const void* const*>(groups));
    write(node.data(),0,std::uintptr_t{123});
    std::array<ModelAdmission,2> models{{{123},{456}}};
    REQUIRE(inspectModelQueue(queue.data(),models));
    CHECK(models[0].present);
    CHECK_FALSE(models[0].admitted);
    CHECK_FALSE(models[1].present);
    write(node.data(),0x1E,std::uint8_t{0x40});
    REQUIRE(inspectModelQueue(queue.data(),models));
    CHECK(models[0].admitted);
    write(queue.data(),0x58,std::uint32_t{16385});
    CHECK_FALSE(inspectModelQueue(queue.data(),models));
    const std::uint32_t bits[]{0xffffffff,1};
    CHECK(visibleBitCount(bits,33)==33);
    CHECK(visibleBitCount(bits,31)==31);
    float bone[16]{}; bone[12]=1; bone[13]=2; bone[14]=3;
    CHECK(distance(modelBonePosition(bone,{100,200,300},true),{101,202,303})==0);
    CHECK(distance(modelBonePosition(bone,{100,200,300},false),{1,2,3})==0);
}
