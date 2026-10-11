#version 450
#include "common.glsl"
layout(std140,binding=1) uniform OriginalMesh {vec4 mesh[2935];};
layout(location=0) out vec3 position;
layout(location=1) out vec3 normal;
layout(location=2) out vec2 uv;
layout(location=3) out vec3 tangent;
layout(location=4) out vec3 bitangent;
layout(location=5) out vec3 localPosition;
void main(){
    int i=gl_VertexID*5;
    vec4 p=mesh[i],n=mesh[i+1],t=mesh[i+2],b=mesh[i+3],weights=mesh[i+4];
    vec4 a=bones[int(weights.x)+1],c=bones[int(weights.y)+1];
    vec3 pa=vec3(p.x*a.x,p.y*a.y+a.z,p.z*a.x),pc=vec3(p.x*c.x,p.y*c.y+c.z,p.z*c.x);
    localPosition=(pa*weights.z+pc*weights.w)*transform.z;
    position=rotatePillar(localPosition)+base.xyz;
    normal=normalize(rotatePillar(n.xyz));tangent=rotatePillar(t.xyz);bitangent=rotatePillar(b.xyz);uv=vec2(p.w,n.w);
    gl_Position=projectWorld(position);
}
