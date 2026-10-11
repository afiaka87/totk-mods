// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "native-common.glsl"
#include "native-depth.glsl"
layout(location=0) in vec3 normal;
layout(location=1) in vec3 world;
layout(location=2) in float fade;
layout(location=0) out vec4 colour;
layout(location=1) out vec4 emission;
void main() {
    float fresnel=pow(1-abs(dot(normalize(normal),normalize(eyePosition()-world))),2.0);
    float facet=.75+.25*max(dot(normalize(normal),normalize(vec3(-.6,1,1))),0.0);
    vec3 yellow=mix(vec3(1,.79,.035),vec3(1,.57,.015),flags.z*.45);
    float coverage=fade*visibleWorld(world);
    vec3 e=yellow*(1.25+fresnel*.65)*facet*coverage;
    colour=vec4(e,coverage);emission=vec4(e,coverage);
}
