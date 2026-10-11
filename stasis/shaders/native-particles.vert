// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "native-common.glsl"
layout(location=0) out vec2 particleUv;
layout(location=1) out float age;
layout(location=2) out float energy;
layout(location=3) out float seed;
layout(location=4) out vec3 world;
float hash(float n) {return fract(sin(n*127.1+311.7)*43758.5453);}
void main() {
    // See native-chain.vert: integer division emits unsupported IMAD (reg).
    float vertex=float(gl_VertexID),id=floor((vertex+.25)/6.0),c=vertex-id*6.0;
    particleUv=vec2((c==1||c==2||c==4)?1:-1,(c==2||c==4||c==5)?1:-1);
    seed=hash(id+7);
    vec3 right=vec3(world0.x,world1.x,world2.x),up=vec3(world0.y,world1.y,world2.y);
    vec3 p=centrePosition();float size=.03;
    if(effects.w<.5) {
        age=effects.x-hash(id+71)*.11;
        float a=hash(id+19)*6.2831853;
        p+=(right*cos(a)+up*sin(a))*(.8+max(age,0.0)*(1.1+seed*2.6));
        p.y-=age*age*.25;
        energy=step(0.0,age)*(1-smoothstep(.25,.9,age));
        size=.040+.070*hash(id+15);
    } else {
        float sampleId=floor((id+.25)/7.0);
        int sampleIndex=int(min(sampleId,max(effects.z-1,0.0)));
        vec4 samplePoint=trail[sampleIndex];
        vec4 previous=trail[max(sampleIndex-1,0)];
        vec3 velocity=(samplePoint.xyz-previous.xyz)/max(samplePoint.w-previous.w,.033333);
        age=effects.y-samplePoint.w-(id-sampleId*7.0)*.0046;
        vec3 drift=vec3(hash(id+3)-.5,hash(id+29)-.5,hash(id+67)-.5);
        p=samplePoint.xyz-velocity/max(length(velocity),.001)*trailParams.x;
        p+=drift*(.18+max(age,0.0)*2.1)-velocity*max(age,0.0)*.09;
        p.y-=max(age,0.0)*max(age,0.0)*.55;
        float life=.75+hash(id+13)*1.1;
        energy=step(0.0,age)*(1-smoothstep(life*.35,life,age));
        size=.008+.022*seed;
        if(seed<.23) size=.18+.12*hash(id+81);
    }
    world=p+(right*particleUv.x+up*particleUv.y)*size;
    gl_Position=projectWorld(world);
}
