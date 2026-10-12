// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "post.glsl"
layout(binding=0) uniform sampler2D source;
layout(location=0) in vec2 screenUv;
layout(location=0) out vec4 result;
void main(){
    vec2 pixel=floor(screenUv*sizes.zw);
    vec3 sum=vec3(0);float total=0;
    for(int i=-6;i<=6;++i){
        float weight=exp(-float(i*i)/8.0);
        sum+=sampleImage(source,(pixel+kernel.xy*float(i)+.5)/sizes.xy,sizes.xy).rgb*weight;
        total+=weight;
    }
    result=vec4(sum/total,0);
}
