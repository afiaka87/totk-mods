#version 450
#include "common.glsl"
layout(binding=0) uniform sampler2D sceneDepth;
layout(location=0) in vec3 position;
layout(location=1) in vec2 local;
layout(location=2) in float opacity;
layout(location=3) flat in float kind;
layout(location=0) out vec4 result;
void main(){
    float depth=dimensions.z+texelFetch(sceneDepth,ivec2(gl_FragCoord.xy),0).r*(dimensions.w-dimensions.z);
    float visible=step(-viewPosition(position).z,depth+.06);
    vec2 q=abs(local);float distance=length(local);
    float star=exp(-q.x*50.0)*pow(max(0.0,1.0-q.y),2.0)+exp(-q.y*50.0)*pow(max(0.0,1.0-q.x),2.0);
    float diagonal=exp(-abs(q.x-q.y)*60.0)*pow(max(0.0,1.0-distance),3.0);
    float shard=(1.0-smoothstep(.38,.48,q.x+q.y))*(.55+.45*step(local.x,local.y));
    float alpha=min(1.0,(kind<.5?star+diagonal*.35:shard*.65+star*.25))*opacity*visible;
    result=vec4(vec3(.38,.9,1.0)*alpha*(kind<.5?2.0:1.0),alpha);
}
