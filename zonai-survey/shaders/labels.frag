// SPDX-License-Identifier: MIT
#version 450
layout(location=0) in vec2 pixel;
layout(location=1) flat in uvec4 tile;
layout(location=0) out vec4 outColor;
layout(std140,binding=0) uniform Atlas { uvec4 pixels[3392]; };
float coverage(ivec2 p) {
    // The pinned compiler mislowers integer clamp to float MOV_SAT.
    if(p.x<0) p.x=0;
    if(p.y<0) p.y=0;
    if(p.x>=int(tile.y)) p.x=int(tile.y)-1;
    if(p.y>=int(tile.z)) p.y=int(tile.z)-1;
    uint address=tile.x+uint(p.y)*tile.y+uint(p.x);
    // Dynamic vector indexing loses the component offset in the pinned compiler.
    uvec4 words=pixels[address/16u];
    uint lane=(address/4u)%4u;
    uint word=words.x;
    if(lane==1u) word=words.y;
    if(lane==2u) word=words.z;
    if(lane==3u) word=words.w;
    return float((word>>((address%4u)*8u))&255u)/255.0;
}
void main() {
    ivec2 p=ivec2(floor(pixel));
    vec2 f=fract(pixel);
    float a=mix(mix(coverage(p),coverage(p+ivec2(1,0)),f.x),
                mix(coverage(p+ivec2(0,1)),coverage(p+ivec2(1,1)),f.x),f.y);
    outColor=vec4(float(tile.w&255u),float((tile.w>>8u)&255u),
                  float((tile.w>>16u)&255u),float(tile.w>>24u))/255.0;
    outColor.a*=a;
}
