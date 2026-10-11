// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "native-common.glsl"
#include "native-private.glsl"
layout(location=0) in vec2 screenUv;
layout(location=0) out vec4 colour;
layout(binding=2) uniform sampler2D source;
void main() {
    vec2 pixel=floor(screenUv*pipeline.xy);
    vec3 sum=vec3(0);float total=0;
    // Contiguous taps avoid ghost images from dilating a five-tap kernel.
    for(int i=-6;i<=6;++i) {
        float weight=exp(-float(i*i)/8.0);
        sum+=texelFetch(source,privatePixel(pixel+pipeline.zw*float(i),pipeline.xy),0).rgb*weight;
        total+=weight;
    }
    colour=vec4(sum/total,0);
}
