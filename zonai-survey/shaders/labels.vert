// SPDX-License-Identifier: MIT
#version 450
struct Quad { vec4 rect; uvec4 tile; };
layout(std140,binding=0) uniform Labels { Quad quads[128]; };
layout(location=0) out vec2 pixel;
layout(location=1) flat out uvec4 tile;
void main() {
    // Exact for IDs 0..767 (128 six-vertex quads); /6 emits unsupported IMAD.HI.
    uint index=(uint(gl_VertexID)*683u)>>12u;
    uint corner=uint(gl_VertexID)-index*6u;
    vec2 uv=vec2((corner==1u || corner==2u || corner==4u) ? 1.0 : 0.0,
                 (corner==2u || corner==4u || corner==5u) ? 1.0 : 0.0);
    Quad q=quads[index];
    vec2 pos=q.rect.xy+uv*q.rect.zw;
    gl_Position=vec4(pos.x/640.0-1.0,1.0-pos.y/360.0,0.0,1.0);
    pixel=uv*vec2(q.tile.yz)-0.5;
    tile=q.tile;
}
