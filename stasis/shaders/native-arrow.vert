// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "native-common.glsl"
layout(location=0) out vec2 arrowUv;
layout(location=1) out vec3 world;
void main() {
    vec2 uv=vec2((gl_VertexID==1||gl_VertexID==2||gl_VertexID==4)?1:0,
                 (gl_VertexID==2||gl_VertexID==4||gl_VertexID==5)?1:0);
    float extent=2.9643395*availableMin[1].w,width=.33875746*availableMin[0].w;
    arrowUv=vec2((uv.x*2-1)*width*1.25,uv.y*(extent+.12)-.06);
    vec3 axis=normalize(availableMin[3].xyz),right=cross(axis,eyePosition()-availableMin[0].xyz);
    right=length(right)>.0001?normalize(right):availableMin[1].xyz;
    world=availableMin[0].xyz+axis*arrowUv.y+right*arrowUv.x;
    gl_Position=projectWorld(world);
}
