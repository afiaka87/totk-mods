// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "post.glsl"
layout(binding=0) uniform sampler2D surface;
layout(binding=1) uniform sampler2D narrowBloom;
layout(binding=2) uniform sampler2D wideBloom;
layout(location=0) in vec2 screenUv;
layout(location=0) out vec4 result;
void main(){
    vec4 base=sampleImage(surface,screenUv,sizes.xy);
    vec3 bloom=sampleImage(narrowBloom,screenUv,ceil(sizes.xy/2.0)).rgb*.48+
               sampleImage(wideBloom,screenUv,ceil(sizes.xy/4.0)).rgb*.42;
    result=vec4(max(base.rgb+bloom,vec3(0)),clamp(base.a,0.0,1.0));
}
