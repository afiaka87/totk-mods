// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "native-common.glsl"
layout(location=0) out vec3 normal;
layout(location=1) out vec3 world;
layout(location=2) out float fade;
const float tau=6.283185307;
vec3 direction(float i) {
    if(i==0)return normalize(vec3(1,.05,.38));
    if(i==1)return normalize(vec3(-1,-.08,-.35));
    if(i==2)return normalize(vec3(.34,1,.42));
    // The lower-left strand travels toward the viewer above the ground. Its old downward axis entered the floor before clearing the object.
    if(i==3)return normalize(vec3(-.70,-.08,1.0));
    if(i==4)return normalize(vec3(-.9,.66,.12));
    return normalize(vec3(.9,-.66,-.12));
}
vec2 curve(float t) {
    vec2 p=vec2(cos(t),sin(t));
    return sign(p)*pow(abs(p),vec2(.62))*vec2(.118,.073);
}
void main() {
    // Integer division compiles to IMAD.HI, which Citron rejects. Every index is an exact float; the .25 keeps reciprocal rounding away from quotient boundaries.
    float id=float(gl_VertexID),link=floor((id+.25)/2880.0),vertex=id-link*2880.0;
    float face=floor((vertex+.25)/6.0),corner=vertex-face*6.0;
    float a=floor((face+.25)/10.0),b=face-a*10.0;
    float da=(corner==1||corner==2||corner==4)?1:0;
    float db=(corner==2||corner==4||corner==5)?1:0;
    float t=(a+da)*tau/48.0,u=(b+db)*tau/10.0;
    vec2 p=curve(t),tan=normalize(curve(t+.005)-curve(t-.005));
    vec2 outwards=vec2(tan.y,-tan.x);
    float lane=floor((link+.25)/46.0),index=link-lane*46.0;
    vec3 axis=direction(lane),ref=abs(axis.y)>.9?vec3(1,0,0):vec3(0,1,0);
    vec3 side=normalize(cross(axis,ref)),up=cross(side,axis);
    float angle=.42+(index-floor(index*.5)*2.0)*tau*.25;
    vec3 width=side*cos(angle)+up*sin(angle),thickness=-side*sin(angle)+up*cos(angle);
    normal=normalize((axis*outwards.x+width*outwards.y)*cos(u)+thickness*sin(u));
    float distance=(index*.204+.04)*1.0;
    vec3 anchor=centrePosition()+axis*distance;
    float breakAge=flags.y>.5?0.0:(1.0-timing.w)*.5;
    float seed=fract(sin((index+lane*97)*91.73)*43758.5);
    anchor+=(side*(seed-.5)*3+up*(.3-seed)*2)*breakAge*breakAge;
    world=anchor+(axis*p.x+width*p.y+normal*.019)*1.0;
    fade=(1-smoothstep(.03,.38,breakAge))*(flags.y>.5?smoothstep(0.0,.12,effects.x):1.0);
    gl_Position=projectWorld(world);
}
