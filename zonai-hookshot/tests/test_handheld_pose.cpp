#include <doctest.h>
#include <initializer_list>
#include "HandheldPose.hpp"
#include "RestingLeftArm.hpp"
#include "HandheldEffects.hpp"
using namespace zonai_hookshot::pure;
namespace pose=zonai_hookshot::pure::handheld;

namespace {
pose::Matrix translated(Vec3 p) {pose::Matrix m{};m.v[3]=p.x;m.v[7]=p.y;m.v[11]=p.z;return m;}
pose::Matrix scaled(pose::Matrix m,Vec3 s) {
    for(int row=0;row<3;++row){m.v[row*4]*=s.x;m.v[row*4+1]*=s.y;m.v[row*4+2]*=s.z;}
    return m;
}
}

TEST_CASE("native local matrix roundtrip retains separate scale and translated parents") {
    for(Vec3 scale:{Vec3{1,1,1},Vec3{1.25f,0.8f,1.1f}}) {
        pose::Matrix parent{};REQUIRE(pose::rotationBetween({1,0,0},{1,2,-3},parent));
        parent=pose::compose(translated({1700,143,-2220}),scaled(parent,{1.2f,0.9f,1.1f}));
        pose::Matrix local{};REQUIRE(pose::rotationBetween({1,0,0},{-2,1,1},local));
        local=pose::compose(translated({-0.24f,0.03f,0}),local);
        const auto world=pose::compose(parent,scaled(local,scale));
        pose::Matrix restored{};REQUIRE(pose::localFromWorld(parent,world,scale,restored));
        for(unsigned i=0;i<12;++i)CHECK(restored.v[i]==doctest::Approx(local.v[i]).epsilon(0.001));
    }
    pose::Matrix out{},singular{};singular.v[0]=0;
    CHECK_FALSE(pose::localFromWorld(singular,{}, {1,1,1},out));
    CHECK_FALSE(pose::localFromWorld({}, {}, {0,1,1},out));
}

TEST_CASE("local shoulder and wrist aim preserves arm reach and hand scale") {
    // Native Link right-arm translations: Arm_2_R -0.24, Wrist_R -0.277184.
    // Bind translations from the clean Link model; animation rotates these joints.
    const auto parent=translated({1700,144,-2220});
    const auto shoulderLocal=translated({-0.15f,0,-0.01074f});
    const auto elbowLocal=translated({-0.24f,0,0});
    const auto wristLocal=translated({-0.277184f,0,0});
    const auto shoulder=pose::compose(parent,shoulderLocal);
    const auto elbow=pose::compose(shoulder,elbowLocal);
    const auto wrist=pose::compose(elbow,wristLocal);
    for(Vec3 offset:{Vec3{20,5,10},Vec3{-12,-8,3},Vec3{0,30,0},Vec3{0,-30,0}}) {
        const auto target=add(pose::position(shoulder),offset);
        pose::ArmLocals aimed{};
        REQUIRE(pose::aimLocals(parent,shoulder,elbow,wrist,{1,1,1},{1,1,1},target,aimed));
        const auto bodyShoulder=pose::compose(parent,aimed.shoulder);
        const auto bodyElbow=pose::compose(bodyShoulder,elbowLocal);
        const auto bodyWrist=pose::compose(bodyElbow,aimed.wrist);
        for(Vec3 axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})
            CHECK(length(pose::vector(bodyWrist,axis))==doctest::Approx(1).epsilon(0.001));
        CHECK(dot(cross(pose::vector(bodyWrist,{1,0,0}),pose::vector(bodyWrist,{0,1,0})),
                  pose::vector(bodyWrist,{0,0,1}))==doctest::Approx(1).epsilon(0.001));
        CHECK(distance(pose::position(bodyWrist),pose::position(bodyShoulder))==doctest::Approx(0.517184f).epsilon(0.001));
        Vec3 actual{},expected{};
        REQUIRE(normalize(sub(pose::position(bodyWrist),pose::position(bodyShoulder)),actual));
        REQUIRE(normalize(offset,expected));
        CHECK(distance(actual,expected)<0.001f);
        // Native right index/middle-finger roots extend along wrist -X.
        // Matching Weapon_R +X to the barrel reverses this and points at Link.
        Vec3 fingers{};REQUIRE(normalize(pose::vector(bodyWrist,{-1,0,0}),fingers));
        CHECK(dot(fingers,expected)>0.9f);
    }
}
TEST_CASE("handheld rotation preserves lengths and handles opposite directions") {
    for(Vec3 target: {Vec3{0,0,1},Vec3{-1,0,0},Vec3{0,1,0},Vec3{1,0,0},Vec3{-1,2,3}}) {
        pose::Matrix r{};
        REQUIRE(pose::rotationBetween({1,0,0},target,r));
        Vec3 normalized{};REQUIRE(normalize(target,normalized));
        CHECK(distance(pose::vector(r,{1,0,0}),normalized)<0.0001f);
        CHECK(length(pose::vector(r,{0,1,0}))==doctest::Approx(1));
        CHECK(distance(pose::point(pose::around(r,{3,4,5}),{3,4,5}),{3,4,5})<0.0001f);
    }
    pose::Matrix r{};CHECK_FALSE(pose::rotationBetween({}, {1,0,0},r));
}
TEST_CASE("aim preserves native glide and climb poses until firing") {
    for(auto phase:{Phase::Targeting,Phase::Confirming}) {
        const auto ground=pose::presentation(phase,false,false);
        CHECK(ground.rightArm);CHECK_FALSE(ground.leftArm);CHECK_FALSE(ground.gliderHidden);
        const auto glide=pose::presentation(phase,true,false);
        CHECK(glide.rightArm);CHECK(glide.glideSteering);CHECK_FALSE(glide.leftArm);CHECK_FALSE(glide.gliderHidden);
        const auto climb=pose::presentation(phase,false,true);
        CHECK(climb.track);CHECK_FALSE(climb.rightArm);CHECK_FALSE(climb.leftArm);CHECK_FALSE(climb.glideSteering);
    }
    for(auto phase:{Phase::ChainLaunch,Phase::PositionCruise}) {
        const auto shot=pose::presentation(phase,true,false);
        CHECK(shot.track);CHECK(shot.rightArm);CHECK(shot.leftArm);CHECK(shot.gliderHidden);CHECK_FALSE(shot.glideSteering);
    }
    const auto arrival=pose::presentation(Phase::Capture,true,false);
    CHECK(arrival.track);CHECK_FALSE(arrival.rightArm);CHECK_FALSE(arrival.leftArm);CHECK(arrival.gliderHidden);
    for(auto phase:{Phase::Idle,Phase::Arming,Phase::Cooldown}) {
        const auto off=pose::presentation(phase,true,false);
        CHECK_FALSE(off.track);CHECK_FALSE(off.rightArm);CHECK_FALSE(off.leftArm);
        CHECK_FALSE(off.gliderHidden);CHECK_FALSE(off.glideSteering);
    }
}
TEST_CASE("ground turn speed accelerates gradually and brakes without a jump") {
    for(float rate:{30.f,60.f,120.f}) {
        float speed=0;
        for(unsigned i=0;i<unsigned(rate);++i) {
            const float next=pose::smoothTurnSpeed(speed,12.f,1/rate);
            CHECK(next>=speed);CHECK(next<=1.570797f);
            CHECK(next-speed<=6.283186f/rate+0.00001f);
            speed=next;
        }
        CHECK(speed==doctest::Approx(1.57079633f));
        for(unsigned i=0;i<unsigned(rate);++i) {
            const float next=pose::smoothTurnSpeed(speed,0,1/rate);
            CHECK(next<=speed);CHECK(next>=0);
            CHECK(speed-next<=6.283186f/rate+0.00001f);
            speed=next;
        }
        CHECK(speed==0);
    }
    CHECK(pose::smoothTurnSpeed(0,-12,1/60.f)<0);
}
TEST_CASE("glide steering maps world target onto camera-relative Npad directions") {
    for(Vec3 camera:{Vec3{0,0,1},Vec3{1,0,0},Vec3{-1,0,-1}}) {
        Vec3 forward{};REQUIRE(normalize(camera,forward));const Vec3 right{forward.z,0,-forward.x};
        for(Vec3 desired:{forward,right,mul(forward,-1),mul(right,-1),add(forward,right)}) {
            float x{},y{};REQUIRE(pose::glideStick(camera,desired,x,y));
            Vec3 expected{};REQUIRE(normalize(desired,expected));
            CHECK(distance(add(mul(right,x),mul(forward,y)),expected)<0.00001f);
            CHECK(x*x+y*y==doctest::Approx(1));
        }
    }
}
TEST_CASE("hand origin and aim follow the wrist even with a rotated native grip") {
    const auto shoulder=translated({0,1.4f,0});
    const auto elbow=pose::compose(shoulder,translated({-0.24f,0,0}));
    pose::Matrix wristRotation{};REQUIRE(pose::rotationBetween({-1,0,0},{1,1,0},wristRotation));
    const auto wrist=pose::compose(pose::compose(elbow,translated({-0.277184f,0,0})),wristRotation);
    pose::ArmLocals out{};
    const Vec3 target{0,1.4f,30};
    REQUIRE(pose::aimLocals({},shoulder,elbow,wrist,{1,1,1},{1,1,1},target,out));
    const auto finalElbow=pose::compose(out.shoulder,translated({-0.24f,0,0}));
    const auto finalWrist=pose::compose(finalElbow,out.wrist);
    Vec3 finger{},aim{};
    REQUIRE(normalize(sub(pose::handOrigin(finalWrist),pose::position(finalWrist)),finger));
    REQUIRE(normalize(sub(target,pose::position(finalWrist)),aim));
    CHECK(dot(finger,aim)>0.9999f);
    CHECK(distance(pose::handOrigin(finalWrist),pose::position(finalWrist))==doctest::Approx(0.1f));
}

TEST_CASE("aim cone bounds the arm in all directions relative to Link") {
    for(Vec3 forward:{Vec3{0,0,1},Vec3{1,0,0},Vec3{-1,0,-1}}) {
        Vec3 f{};REQUIRE(normalize(forward,f));const Vec3 right{f.z,0,-f.x};
        for(int yaw=-180;yaw<=180;yaw+=10)for(int pitch=-90;pitch<=90;pitch+=15) {
            const float y=yaw*0.0174532925f,p=pitch*0.0174532925f;
            const auto desired=add(mul(add(mul(f,std::cos(y)),mul(right,std::sin(y))),std::cos(p)),Vec3{0,std::sin(p),0});
            pose::AimCone result{};REQUIRE(pose::constrainAim(f,desired,result));
            CHECK(length(result.direction)==doctest::Approx(1));
            CHECK(std::fabs(std::atan2(dot(result.direction,right),dot(result.direction,f)))<=pose::kAimYaw+0.00001f);
            const float elevation=std::asin(result.direction.y);
            CHECK(elevation>=pose::kAimDown-0.00001f);CHECK(elevation<=pose::kAimUp+0.00001f);
            CHECK(dot(result.direction,f)>0);
        }
    }
    CHECK_FALSE(pose::needsBodyTurn(0.30f,false));CHECK(pose::needsBodyTurn(-0.36f,false));
    CHECK(pose::needsBodyTurn(0.20f,true));CHECK_FALSE(pose::needsBodyTurn(-0.13f,true));
    pose::AimCone result{};CHECK_FALSE(pose::constrainAim({}, {1,0,0},result));
}
TEST_CASE("weapon effect masks restore only the original live emitter") {
    pose::EffectMasks<2> masks;
    REQUIRE(masks.remember({1,11,7},0x81));REQUIRE(masks.remember({2,12,8},0x20));
    CHECK_FALSE(masks.remember({3,13,9},0xFF));CHECK_FALSE(masks.remember({1,11,7},0));
    CHECK(masks.restore({1,11,7})==0x81);CHECK_FALSE(masks.restore({1,11,7}));
    CHECK_FALSE(masks.restore({2,12,9})); // recycled emitter must not receive an old mask
    REQUIRE(masks.remember({3,13,9},0));CHECK(masks.restore({3,13,9})==0);
}
TEST_CASE("Ultrahand glow matches native skin and marking curve keys") {
    CHECK(pose::ultrahandPulse(0,2,6)==6);CHECK(pose::ultrahandPulse(24,2,6)==2);
    CHECK(pose::ultrahandPulse(48,2,6)==6);CHECK(pose::ultrahandPulse(72,2,6)==2);
    CHECK(pose::ultrahandPulse(96,2,6)==6);CHECK(pose::ultrahandPulse(12,2,6)==4);
    CHECK(pose::ultrahandPulse(0,1,20)==20);CHECK(pose::ultrahandPulse(24,1,20)==1);
    CHECK(pose::ultrahandPulse(36,1,20)==doctest::Approx(10.5));
}

TEST_CASE("recorded relaxed arm preserves live proportions and proper rotations") {
    for(const auto& bone:pose::kRestingLeftArm) {
        const auto live=translated({0.12f,0.02f,-0.03f});
        const auto local=pose::restingLocal(live,bone.rotation);
        CHECK(distance(pose::position(local),pose::position(live))<0.000001f);
        const auto x=pose::vector(local,{1,0,0}),y=pose::vector(local,{0,1,0}),z=pose::vector(local,{0,0,1});
        CHECK(dot(x,y)==doctest::Approx(0).epsilon(0.00001));
        CHECK(dot(cross(x,y),z)==doctest::Approx(1).epsilon(0.00001));
    }
    const auto& shoulder=pose::kRestingLeftArm[1].rotation;
    const auto& elbow=pose::kRestingLeftArm[3].rotation;
    const auto upper=pose::vector(shoulder,{1,0,0});
    const auto lower=pose::vector(pose::compose(shoulder,elbow),{1,0,0});
    CHECK(dot(upper,lower)>0.89f);CHECK(dot(upper,lower)<0.92f);
}
