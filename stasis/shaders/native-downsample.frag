// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "native-common.glsl"
#include "native-private.glsl"
layout(location=0) in vec2 screenUv;
layout(location=0) out vec4 colour;
layout(binding=2) uniform sampler2D source;
void main() {
    float scale=pipeline.z;
    vec2 pixel=floor(screenUv*ceil(pipeline.xy/scale))*scale;
    vec3 sum=vec3(0);float total=0;
    for(int y=0;y<4;++y) for(int x=0;x<4;++x) {
        if(float(x)<scale&&float(y)<scale) {
            sum+=texelFetch(source,privatePixel(pixel+vec2(x,y),pipeline.xy),0).rgb;
            total+=1;
        }
    }
    colour=vec4(sum/total,0);
}
