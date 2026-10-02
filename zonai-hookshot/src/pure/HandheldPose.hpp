// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "ChainPresentation.hpp"
#include "HookshotState.hpp"
#include <cmath>

namespace zonai_hookshot::pure::handheld {
struct Matrix { float v[12]{1,0,0,0, 0,1,0,0, 0,0,1,0}; };
inline Vec3 position(const Matrix& a) { return {a.v[3],a.v[7],a.v[11]}; }
inline Vec3 vector(const Matrix& a, Vec3 p) {
    return {a.v[0]*p.x+a.v[1]*p.y+a.v[2]*p.z,
            a.v[4]*p.x+a.v[5]*p.y+a.v[6]*p.z,
            a.v[8]*p.x+a.v[9]*p.y+a.v[10]*p.z};
}
inline Vec3 point(const Matrix& a, Vec3 p) { return add(vector(a,p),position(a)); }
inline Matrix compose(const Matrix& a,const Matrix& b) {
    Matrix c{};
    for(int r=0;r<3;++r) for(int j=0;j<4;++j) {
        c.v[r*4+j]=j==3?a.v[r*4+3]:0;
        for(int k=0;k<3;++k) c.v[r*4+j]+=a.v[r*4+k]*b.v[k*4+j];
    }
    return c;
}
inline Matrix inverseRigid(const Matrix& a) {
    Matrix b{};
    for(int r=0;r<3;++r) for(int c=0;c<3;++c) b.v[r*4+c]=a.v[c*4+r];
    const auto p=mul(vector(b,position(a)),-1);
    b.v[3]=p.x; b.v[7]=p.y; b.v[11]=p.z;
    return b;
}
inline bool inverseAffine(const Matrix& a, Matrix& out) {
    const Vec3 x{a.v[0],a.v[4],a.v[8]}, y{a.v[1],a.v[5],a.v[9]}, z{a.v[2],a.v[6],a.v[10]};
    const float determinant=dot(x,cross(y,z));
    if(!std::isfinite(determinant)||std::fabs(determinant)<1e-8f)return false;
    const auto r0=mul(cross(y,z),1/determinant),r1=mul(cross(z,x),1/determinant),r2=mul(cross(x,y),1/determinant);
    out={{r0.x,r0.y,r0.z,0,r1.x,r1.y,r1.z,0,r2.x,r2.y,r2.z,0}};
    const auto p=mul(vector(out,position(a)),-1);
    out.v[3]=p.x;out.v[7]=p.y;out.v[11]=p.z;
    return true;
}
// Native local matrices store rotation/translation separately from scale.
inline bool localFromWorld(const Matrix& parent,const Matrix& world,Vec3 scale,Matrix& out) {
    Matrix inverse{};
    if(!inverseAffine(parent,inverse)||!finite3(scale)||std::fabs(scale.x)<1e-6f||
       std::fabs(scale.y)<1e-6f||std::fabs(scale.z)<1e-6f)return false;
    out=compose(inverse,world);
    for(int r=0;r<3;++r){out.v[r*4]/=scale.x;out.v[r*4+1]/=scale.y;out.v[r*4+2]/=scale.z;}
    for(float value:out.v)if(!std::isfinite(value))return false;
    return true;
}
inline Matrix around(Matrix r,Vec3 pivot) {
    const auto t=sub(pivot,vector(r,pivot)); r.v[3]=t.x;r.v[7]=t.y;r.v[11]=t.z;return r;
}
inline bool rotationBetween(Vec3 from,Vec3 to,Matrix& r) {
    Vec3 a{},b{}; if(!normalize(from,a)||!normalize(to,b))return false;
    float c=clamp(dot(a,b),-1,1); Vec3 axis=cross(a,b);
    if(c>0.999999f) {r={};return true;}
    if(c< -0.9999f) {
        if(!normalize(cross(a,{1,0,0}),axis))normalize(cross(a,{0,1,0}),axis);
        r={};
        const float p[3]{axis.x,axis.y,axis.z};
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.v[i*4+j]=2*p[i]*p[j]-(i==j?1.f:0.f);
        return true;
    }
    const float x=axis.x,y=axis.y,z=axis.z,k=1/(1+c);
    r={{c+x*x*k,x*y*k-z,x*z*k+y,0,
        y*x*k+z,c+y*y*k,y*z*k-x,0,
        z*x*k-y,z*y*k+x,c+z*z*k,0}};
    return true;
}
// Native index/middle finger roots are about 0.10 m along Wrist_R -X.
inline Vec3 handOrigin(const Matrix& wrist) {return point(wrist,{-0.10f,0,0});}
struct ArmLocals { Matrix shoulder{},wrist{}; };
// Presentation limits, in radians. The body catches up before the arm crosses the chest.
inline constexpr float kAimYaw=0.34906585f; // 20 degrees each way
inline constexpr float kAimDown=-0.95993109f, kAimUp=1.22173048f; // -55 / +70
struct AimCone { Vec3 direction{}; float yaw{},pitch{}; };
inline bool constrainAim(Vec3 forward,Vec3 desired,AimCone& out) {
    forward.y=0;
    Vec3 f{},d{};
    if(!normalize(forward,f)||!normalize(desired,d))return false;
    const Vec3 right{f.z,0,-f.x};
    const float horizontal=std::sqrt(d.x*d.x+d.z*d.z);
    out.yaw=horizontal>0.0001f?std::atan2(dot(d,right),dot(d,f)):0;
    out.pitch=std::atan2(d.y,horizontal);
    const auto yaw=clamp(out.yaw,-kAimYaw,kAimYaw);
    const auto pitch=clamp(out.pitch,kAimDown,kAimUp);
    out.direction=add(mul(add(mul(f,std::cos(yaw)),mul(right,std::sin(yaw))),std::cos(pitch)),
                      Vec3{0,std::sin(pitch),0});
    return finite3(out.direction);
}
inline bool needsBodyTurn(float yaw,bool turning) {
    return std::isfinite(yaw)&&std::fabs(yaw)>(turning?0.13962634f:kAimYaw); // stop at 8 degrees
}
inline float smoothTurnSpeed(float previous,float requested,float seconds) {
    if(!std::isfinite(previous)||!std::isfinite(requested)||!std::isfinite(seconds)||seconds<=0)return 0;
    // Presentation tuning: 90 degrees/s, reached over a quarter second.
    constexpr float speed=1.57079633f,acceleration=6.28318531f;
    const float step=acceleration*clamp(seconds,0,0.05f);
    return previous+clamp(clamp(requested,-speed,speed)-previous,-step,step);
}
inline bool isAiming(Phase phase) {return phase==Phase::Targeting||phase==Phase::Confirming;}
inline bool ownsArm(Phase phase) {
    return isAiming(phase)||phase==Phase::ChainLaunch||phase==Phase::Latched||
           phase==Phase::PositionCruise||phase==Phase::DetachRequest||
           phase==Phase::FallCruise||phase==Phase::GlideHandoff||phase==Phase::Capture;
}
inline bool tracksHand(Phase phase) {return ownsArm(phase)||phase==Phase::GlideTerminal;}
inline bool hidesGlider(Phase phase) {return ownsArm(phase)&&!isAiming(phase);}
struct Presentation {bool track{},rightArm{},leftArm{},gliderHidden{},glideSteering{};};
inline Presentation presentation(Phase phase,bool gliding,bool climbing) {
    return {tracksHand(phase),ownsArm(phase)&&!climbing,
            ownsArm(phase)&&!isAiming(phase)&&gliding&&!climbing,
            hidesGlider(phase),isAiming(phase)&&gliding&&!climbing};
}
inline bool glideStick(Vec3 cameraForward,Vec3 desired,float& x,float& y) {
    cameraForward.y=0;desired.y=0;
    Vec3 forward{},direction{};
    if(!normalize(cameraForward,forward)||!normalize(desired,direction))return false;
    x=dot(direction,{forward.z,0,-forward.x});y=dot(direction,forward);
    return std::isfinite(x)&&std::isfinite(y);
}
// Cubic coefficients from Player_Skin_Animation's native 96-frame Ultrahand loop.
inline float ultrahandPulse(float frame,float low,float high) {
    if(!std::isfinite(frame)||frame<0)return high;
    frame=std::fmod(frame,48.f);
    const float t=std::fmod(frame,24.f)/24.f;
    const float smooth=t*t*(3.f-2.f*t);
    return frame<24.f?high+(low-high)*smooth:low+(high-low)*smooth;
}
inline bool aimLocals(const Matrix& shoulderParent,const Matrix& shoulder,
                      const Matrix& wristParent,const Matrix& wrist,
                      Vec3 shoulderScale,Vec3 wristScale,Vec3 target,ArmLocals& out) {
    Matrix rotation{},handRotation{};
    const auto pivot=position(shoulder);
    if(!rotationBetween(sub(position(wrist),pivot),sub(target,pivot),rotation))return false;
    const auto swing=around(rotation,pivot);
    const auto swungWrist=compose(swing,wrist);
    // Align the actual finger axis directly, retaining the native hand's roll.
    if(!rotationBetween(vector(swungWrist,{-1,0,0}),sub(target,position(swungWrist)),handRotation))return false;
    const auto twist=around(handRotation,position(swungWrist));
    return localFromWorld(shoulderParent,compose(swing,shoulder),shoulderScale,out.shoulder)&&
           localFromWorld(compose(swing,wristParent),compose(twist,swungWrist),wristScale,out.wrist);
}
} // namespace zonai_hookshot::pure::handheld
