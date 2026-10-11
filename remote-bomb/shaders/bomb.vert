#version 450
#include "common.glsl"
layout(std140,binding=1) uniform OriginalMesh {vec4 mesh[3420];};
layout(location=0) out vec3 position;
layout(location=1) out vec3 normal;
layout(location=2) out vec2 uv;
layout(location=3) out vec3 tangent;
layout(location=4) out vec3 bitangent;
void main(){
    int i=gl_VertexID*3;
    vec4 p=mesh[i],n=mesh[i+1],t=mesh[i+2];
    vec3 local=p.xyz;
    if(highlight.w>1.5){
        // Outline shell: pushed out from the mesh centre by a fixed share of the screen height.
        vec3 away=local-outline.xyz;
        float size=length(away);
        float d=max(-viewPosition(worldPosition(local)).z,dimensions.z);
        if(size>1e-4)local+=away/size*outline.w*2.0*d/projection.y;
    }
    position=worldPosition(local);
    normal=normalize(rotateBomb(n.xyz));
    tangent=normalize(rotateBomb(t.xyz));
    bitangent=normalize(cross(normal,tangent))*t.w;
    uv=vec2(p.w,n.w);
    gl_Position=projectWorld(position);
}
