// SPDX-License-Identifier: MIT
#version 450
struct Quad { vec2 position; uint tile; uint color; };
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
    bool icon=(q.tile&262144u)!=0u;
    vec2 extent=vec2(icon ? 16.0 : 22.0);
    vec2 dimensions=vec2(icon ? 32.0 : 16.0);
    vec2 pos=q.position+uv*extent;
    gl_Position=vec4(pos.x/640.0-1.0,1.0-pos.y/360.0,0.0,1.0);
    // A 16-unit cell spans the tile; letter quads add an equal margin on every side.
    vec2 margin=(extent-vec2(16.0))*0.5;
    pixel=(uv*extent-margin)*dimensions*0.0625-0.5;
    tile=uvec4(q.tile&~262144u,uvec2(dimensions),q.color);
}
