// SPDX-License-Identifier: MIT
#version 450
struct Quad { vec4 rect; uvec4 style; };
layout(std140, binding=0) uniform Geometry { Quad quads[128]; };
layout(location=0) out vec2 pixel;
layout(location=1) flat out uvec4 tile;
void main() {
    // Exact quotient for the bounded 128-quad batch; avoids the target compiler's IMAD.HI.
    uint index = (uint(gl_VertexID) * 683u) >> 12u;
    uint corner = uint(gl_VertexID) - index * 6u;
    vec2 uv = vec2((corner == 1u || corner == 2u || corner == 4u) ? 1.0 : 0.0,
              (corner == 2u || corner == 4u || corner == 5u) ? 1.0 : 0.0);
    Quad quad = quads[index];
    vec2 point = quad.rect.xy + uv * quad.rect.zw;
    gl_Position = vec4(point.x / 640.0 - 1.0, 1.0 - point.y / 360.0, 0.0, 1.0);
    pixel = uv * 48.0 - 0.5;
    tile = uvec4(quad.style.x,48u,48u,quad.style.z);
}
