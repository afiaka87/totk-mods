// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "native-common.glsl"
#include "native-depth.glsl"
layout(location=0) in vec2 arrowUv;
layout(location=1) in vec3 world;
layout(location=0) out vec4 colour;
layout(location=1) out vec4 emission;
void main() {
    float length=2.9643395*availableMin[1].w,width=.33875746*availableMin[0].w;
    float x=arrowUv.x,y=arrowUv.y,t=clamp(y/length,0.0,1.0);
    float headLength=length*.325,headStart=length-headLength;
    float stem=availableMin[0].w*(.004+.034*pow(t,1.2)+.033*pow(t,9.0));
    float rel=clamp((y-headStart)/headLength,0.0,1.0);
    float outside=width*pow(1-rel,.86);
    float underside=headStart+headLength*.36*sqrt(max(0.0,1-pow(abs(x)/width,2.0)));
    float shaftDistance=max(abs(x)-stem,max(-y,y-(length-headLength*.20)));
    float headDistance=max(abs(x)-outside,max(underside-y,y-length));
    // A smooth union rounds the concave shoulder into the widening shaft.
    float fillet=.12;
    float join=clamp(.5+.5*(headDistance-shaftDistance)/fillet,0.0,1.0);
    float distance=mix(headDistance,shaftDistance,join)-fillet*join*(1-join);
    float aa=max(viewPosition(world).z*(-2.0)/(abs(projection.x)*dimensions.x),.001);
    float alpha=(1-smoothstep(-aa,aa,distance))*visibleWorld(world);
    float fold=exp(-abs(x)/max(stem*.45,.009));
    float flame=.88+.12*sin(y*17+timing.y*4+sin(x*38+y*2));
    vec3 gold=mix(vec3(1,.64,.018),vec3(1,.34,.008),flags.z);
    vec3 e=(gold*(1.10+.35*fold)+vec3(1,.68,.20)*fold*.24)*flame*(1+timing.z*.7)*alpha;
    colour=vec4(e,alpha);emission=vec4(e*.22,alpha);
}
