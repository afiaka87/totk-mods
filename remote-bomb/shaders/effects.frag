#version 450
#include "common.glsl"
layout(binding=0) uniform sampler2D sceneDepth;
layout(binding=1) uniform sampler2D originalMask;
layout(binding=2) uniform sampler2D originalNoise;
layout(binding=3) uniform sampler2D originalGlow;
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in float particle;
layout(location=0) out vec4 result;
layout(location=1) out vec4 bright;
void main(){
    vec2 q=uv*2.0-1.0;
    float radius=length(q);
    float mask=texture(originalMask,uv).r;
    if(lighting.z<.5){
        vec2 warp=texture(originalMask,uv+vec2(lighting.y*.2,-lighting.y*.13)).rg*2.0-1.0;
        float plasma=texture(originalGlow,uv+warp*.075+lighting.y*.07).r;
        float edge=exp(-abs(radius-.83)*25.0);
        float glow=texture(originalNoise,uv).g;
        mask=(plasma*.45+edge*.8+glow*.35)*(1.0-smoothstep(.97,1.0,radius));
    }
    if(lighting.z>.5&&lighting.z<1.5){
        float band=abs(radius-.8)*12.0;
        mask=texture(originalMask,vec2(.5,clamp(band,0.0,1.0))).r;
        mask*=1.0-smoothstep(.12,.2,abs(radius-.8));
    }
    if(lighting.z>1.5&&lighting.z<2.5)mask=texture(originalMask,vec2(uv.y,1.0-uv.x)).r;
    if(lighting.z>3.5&&lighting.z<4.5){
        vec4 rune=texture(originalGlow,uv);
        mask=max(rune.r,rune.g)*texture(originalMask,uv+lighting.y*.2).r;
    }
    if(lighting.z>5.5&&lighting.z<6.5)mask=texture(originalMask,vec2(1.0)-abs(q)).r;
    if(lighting.z>6.5)mask=texture(originalMask,uv).g;
    float depth=dimensions.z+texelFetch(sceneDepth,ivec2(gl_FragCoord.xy),0).r*(dimensions.w-dimensions.z);
    float alpha=clamp(mask*lighting.w,0.0,1.0);
    if(-viewPosition(position).z>depth+.005)alpha=0.0;
    gl_FragDepth=clamp((-viewPosition(position).z-dimensions.z)/(dimensions.w-dimensions.z),0.0,1.0);
    result=vec4(emission.rgb*alpha,alpha);
    bright=result;
}
