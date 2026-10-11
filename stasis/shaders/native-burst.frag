// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "native-common.glsl"
layout(location=0) in vec2 screenUv;
layout(location=0) out vec4 colour;
layout(location=1) out vec4 emission;
void main() {
    vec2 extent=max(resources.xy,vec2(12));
    vec2 p=(vec2(screenUv.x,1-screenUv.y)*dimensions.xy-target.xy)/extent;
    float r=length(p),a=atan(p.y,p.x);
    float life=flags.y*step(0.0,effects.x)*smoothstep(0.0,.065,effects.x)*(1-smoothstep(.15,1.0,effects.x));
    float radius=.96+.07*smoothstep(0.0,.30,effects.x);
    // Rays and burst at 75% of the approved study's strength; bloom and object fit are unchanged.
    const float referenceScale=.75;
    float teeth=pow(max(0.0,sin(a*11+effects.x*.8)),8.0)*(.12+.58*(.5+.5*sin(a*7+2)))*referenceScale;
    teeth+=pow(max(0.0,sin(a*19-effects.x*1.6+1)),18.0)*.20*referenceScale;
    float shape=1-smoothstep(radius+teeth-.045,radius+teeth+.008,r);
    float edge=shape*smoothstep(radius-.20*referenceScale,radius+.06*referenceScale,r);
    float rays=pow(max(0.0,sin(a*17+sin(a*3)+effects.x*2)),5.0)*exp(-r*r*.65/(referenceScale*referenceScale));
    float veil=exp(-r*r*1.8/(referenceScale*referenceScale))*.23;
    vec3 e=(vec3(1,.79,.05)*(rays*.8+veil)+vec3(1,.95,.54)*edge*5.0)*life*referenceScale;
    colour=vec4(e,0);emission=vec4(e,0);
}
