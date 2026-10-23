// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "native-common.glsl"
#include "native-depth.glsl"
layout(binding=1) uniform sampler2D modelDepth;
layout(location=0) in vec2 screenUv;
layout(location=0) out vec4 outColor;
layout(location=1) out vec4 emission;
const float tau=6.283185307;
float band(float d,float w) {return 1-smoothstep(max(w-1,0.0),w+1,abs(d));}
vec4 over(vec4 a,vec3 rgb,float alpha) {alpha=clamp(alpha,0.0,1.0);return vec4(rgb*alpha,alpha)+a*(1-alpha);}
float modelZ(vec2 uv) {return dimensions.z+texelFetch(modelDepth,pixelAt(uv,ivec2(maskDimensions.xy)),0).r*(dimensions.w-dimensions.z);}
float maskAt(vec2 uv) {
    float d=texelFetch(modelDepth,pixelAt(uv,ivec2(maskDimensions.xy)),0).r;
    if(isnan(d)||isinf(d)||d<=0||d>=1) return 0;
    return step(dimensions.z+d*(dimensions.w-dimensions.z)-.16,sceneMetres(uv));
}
vec3 hash3(vec3 p) {
    return fract(sin(vec3(dot(p,vec3(127.1,311.7,74.7)),dot(p,vec3(269.5,183.3,246.1)),
        dot(p,vec3(113.5,271.9,124.6))))*43758.5453);
}
vec2 fissures(vec3 p) {
    vec3 cell=floor(p),local=fract(p);
    float first=100.0,second=100.0,third=100.0;
    for(int z=-1;z<=1;++z) for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        vec3 offset=vec3(x,y,z),delta=offset+0.15+0.7*hash3(cell+offset)-local;
        float d=dot(delta,delta);
        third=max(second,min(third,d));second=max(first,min(second,d));first=min(first,d);
    }
    return vec2(sqrt(second)-sqrt(first),sqrt(third)-sqrt(first));
}
void main() {
    vec2 p=vec2(screenUv.x,1-screenUv.y)*dimensions.xy;
    vec4 color=vec4(0);vec3 e=vec3(0),gold=vec3(1,.77,.08);
    float blink=1;
    if(extra.x<90&&flags.y>.5) blink=.60+.40*step(0.0,sin(timing.y*18));
    if(resources.w<1.5&&selectedMax.w>.5&&flags.w>.5) {
        float model=maskAt(screenUv);vec2 px=1.0/maskDimensions.xy;
        float l=maskAt(screenUv+vec2(px.x,0)),r=maskAt(screenUv-vec2(px.x,0));
        float u=maskAt(screenUv+vec2(0,px.y)),d=maskAt(screenUv-vec2(0,px.y));
        float inner=model*(1-min(min(l,r),min(u,d)));
        float outer=(1-model)*max(max(l,r),max(u,d));
        float warmth=clamp(flags.z/0.8,0.0,1.0);
        vec3 palette=warmth<.5?mix(vec3(1,.72,.035),vec3(1,.18,.012),warmth*2):mix(vec3(1,.18,.012),vec3(1,.012,.003),(warmth-.5)*2);
        palette=mix(vec3(.72,1,.18),palette,flags.y);
        if(model>0) {
            vec3 w=worldAt(screenUv,modelZ(screenUv));
            vec3 dx=worldAt(screenUv+vec2(px.x,0),modelZ(screenUv+vec2(px.x,0)))-w;
            vec3 dy=worldAt(screenUv+vec2(0,px.y),modelZ(screenUv+vec2(0,px.y)))-w;
            vec3 n=normalize(cross(dx,dy)+vec3(.000001));
            float rim=pow(1-abs(dot(n,normalize(eyePosition()-w))),5.0);
            rim=mix(inner,rim,l*u);
            float diameter=max(max(selectedMax.x-selectedMin.x,selectedMax.y-selectedMin.y),selectedMax.z-selectedMin.z);
            vec3 q=(w-centrePosition())*2.7/max(diameter,.1);
            q+=.15*sin(q.yzx*2.7)+.012*sin(q.zxy*37+timing.y*1.8);
            vec2 f=fissures(q+vec3(7.31,11.73,3.91));
            float aa=clamp(2.7/max(diameter*extra.z,1.0),.005,.06);
            float line=1-smoothstep(.009,.030+aa,f.x),hot=exp(-f.y*f.y/.00045),edge=exp(-f.x*f.x/.0013);
            float spark=.5+.5*sin(q.y*7+q.z*4+timing.y*2);
            e=palette*(line*(1.1+.35*spark)+hot*.75+edge*.20+rim*.8)*(.94+.06*sin(timing.y*2));
            color=vec4(palette*.08+e,.10+.36*line);
        }
        float nearTarget=step(availableMax[0].w-.16,sceneMetres(screenUv));
        vec3 border=palette*(inner+outer*nearTarget)*1.25;
        e+=border;color.rgb+=border;color.a=max(color.a,(inner+outer*nearTarget)*.45);
    }
    emission=vec4(e,0);
    if(resources.w!=1) {
    // In 900-line units, so the ring keeps its screen size at any render height.
    float rowY=effects.w>0?effects.w:.14;
    vec2 hud=(p-vec2(dimensions.x*0.105,dimensions.y*rowY))*(900.0/dimensions.y);
    float hudAngle=atan(hud.x,-hud.y);if(hudAngle<0.0) hudAngle+=tau;
    color=over(color,vec3(0.08,0.06,0.015),band(length(hud)-23.0,4.0)*0.8);
    float progress=flags.y>0.5?timing.x:1.0-extra.y;
    color=over(color,gold,band(length(hud)-23.0,2.5)*step(hudAngle/tau,progress)*0.95*blink);
    float padlock=band(max(abs(hud.x)-8.0,abs(hud.y-5.0)-7.0),1.0)*step(-1.0,hud.y);
    float shackle=band(length(vec2(hud.x,hud.y+3.0))-6.0,1.4)*step(hud.y,-1.0);
    color=over(color,gold,max(padlock,shackle)*0.95);
    if(flags.x>0.5) {
        vec2 c=p-dimensions.xy*0.5;
        float square=band(max(abs(c.x),abs(c.y))-17.0,1.4),corners=step(9.0,min(abs(c.x),abs(c.y)));
        float ticks=max(band(c.x,0.8)*step(21.0,abs(c.y))*step(abs(c.y),27.0),band(c.y,0.8)*step(21.0,abs(c.x))*step(abs(c.x),27.0));
        float dotMarker=1.0-smoothstep(1.0,2.5,length(c));
        color=over(color,vec3(0.03),max(square*corners,ticks)*0.95);
        vec3 reticleColor=(flags.w>0.5?vec3(0.72,1.0,0.18):gold)*(extra.y>0.0?0.35:1.0);
        color=over(color,reticleColor,max(square*corners,max(ticks,dotMarker))*0.94);
    }
    }
    outColor=color;
}
