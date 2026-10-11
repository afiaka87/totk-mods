// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "native-common.glsl"
#include "native-depth.glsl"
layout(location=0) in vec2 particleUv;
layout(location=1) in float age;
layout(location=2) in float energy;
layout(location=3) in float seed;
layout(location=4) in vec3 world;
layout(location=0) out vec4 colour;
layout(location=1) out vec4 emission;
void main() {
    vec2 p=particleUv;
    float d=length(p),a=atan(p.y,p.x);
    float shard=1-smoothstep(.49+.08*sin(a*5+seed*6),.70,d);
    float glow=exp(-d*d*5);
    vec3 rgb=effects.w<.5?vec3(1,.97,.65):mix(vec3(1,.17,.006),vec3(1,.76,.28),exp(-age*2.3));
    float strength=energy*(shard*3.8+glow*.5);
    if(effects.w>.5&&seed<.23) {strength=energy*glow*.10;rgb=vec3(1,.065,.006);}
    vec3 e=rgb*strength*visibleWorld(world);
    colour=vec4(e,0);emission=vec4(e,0);
}
