// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "native-common.glsl"
#include "native-private.glsl"
layout(location=0) in vec2 screenUv;
layout(location=0) out vec4 colour;
layout(binding=3) uniform sampler2D narrowBloom;
layout(binding=4) uniform sampler2D wideBloom;
void main() {
    // Stasis is already drawn on the game's frame; this adds only its bloom, keeping alpha.
    vec3 bloom=privateSample(narrowBloom,screenUv,ceil(dimensions.xy/2)).rgb*.48+
        privateSample(wideBloom,screenUv,ceil(dimensions.xy/4)).rgb*.42;
    // The game owns final exposure, tone mapping and transfer to the display.
    colour=vec4(max(bloom,vec3(0)),0);
}
