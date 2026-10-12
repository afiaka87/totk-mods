#version 450
#include "common.glsl"
layout(binding=0) uniform sampler2D sceneDepth;
layout(binding=1) uniform sampler2D originalAlbedo;
layout(binding=2) uniform sampler2D originalEmission;
layout(binding=3) uniform sampler2D originalNormal;
layout(binding=4) uniform sampler2D originalSpecular;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec3 tangent;
layout(location=4) in vec3 bitangent;
layout(location=0) out vec4 result;
layout(location=1) out vec4 bright;
void main(){
    vec2 depthScale=vec2(privateDepth.z>0?privateDepth.z:1,privateDepth.w>0?privateDepth.w:1);
    float depth=dimensions.z+texelFetch(sceneDepth,ivec2(gl_FragCoord.xy*depthScale),0).r*
                (dimensions.w-dimensions.z);
    vec4 albedo=texture(originalAlbedo,uv);
    vec2 mapped=texture(originalNormal,uv).rg*2.0-1.0;
    vec3 n=normalize(tangent*mapped.x+bitangent*mapped.y+
                     normal*sqrt(max(0.0,1.0-dot(mapped,mapped))));
    vec3 towardEye=normalize(eyePosition()-position);
    vec3 light=normalize(lighting.xyz);
    float diffuse=.24+.76*max(dot(n,light),0.0);
    float specular=texture(originalSpecular,uv).r;
    float gloss=pow(max(dot(n,normalize(towardEye+light)),0.0),40.0);
    vec3 reflected=reflect(-towardEye,n);
    vec3 sky=mix(vec3(.06,.095,.13),vec3(.32,.46,.6),smoothstep(-.3,.9,reflected.y));
    vec3 color=pow(albedo.rgb,vec3(2.2))*diffuse+specular*(sky*.22+gloss*.8);
    float mask=texture(originalEmission,uv).r;
    vec3 glow=emission.rgb*emission.w*mask;
    color+=glow;
    float distance=-viewPosition(position).z;
    // The outline shell keeps only its far side, so the body drawn after it covers the inside.
    bool shell=highlight.w>1.5,facing=dot(normal,towardEye)>0.0;
    if((lighting.w>.5&&albedo.a<.5)||distance<dimensions.z||distance>depth+.005||facing==shell)discard;
    gl_FragDepth=privateDepthOf(distance);
    if(shell){
        color=highlight.rgb;
        glow=highlight.rgb*.35;
    }else if(highlight.w>.5){
        float rim=smoothstep(.35,.95,1.0-max(dot(normalize(normal),towardEye),0.0));
        color=mix(color,highlight.rgb,.3+.5*rim);
        glow+=highlight.rgb*.35*rim;
    }
    result=vec4(color,1);
    bright=vec4(glow,1);
}
