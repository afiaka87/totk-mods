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
float bilinear(vec2 at) {
    ivec2 p=ivec2(floor(at));
    vec2 f=fract(at);
    return mix(mix(coverage(p),coverage(p+ivec2(1,0)),f.x),
               mix(coverage(p+ivec2(0,1)),coverage(p+ivec2(1,1)),f.x),f.y);
}
// Letter cells are distance fields: 128 is the edge, 16 units per texel, inside is positive.
float inside(float field) { return (field*255.0-128.0)/16.0; }
const float kOutlineTexels=0.6, kOutlineAlpha=0.45;
const vec2 kShadowOffset=vec2(2.0);
const float kShadowSoftness=1.5, kShadowAlpha=0.65;
void main() {
    float a=bilinear(pixel);
    outColor=vec4(float(tile.w&255u),float((tile.w>>8u)&255u),
                  float((tile.w>>16u)&255u),float(tile.w>>24u))/255.0;
    if(tile.y==16u) {
        float footprint=max(abs(dFdx(pixel.x)),0.0001);
        float edge=inside(a);
        float fill=clamp(0.5+edge/footprint,0.0,1.0);
        float outline=clamp(0.5+(edge+kOutlineTexels)/footprint,0.0,1.0)*kOutlineAlpha;
        float shadow=clamp(0.5+inside(bilinear(pixel-kShadowOffset))/kShadowSoftness,0.0,1.0)*kShadowAlpha;
        // Text over a dark backing: the backing only shows where the letter does not.
        a=fill+max(outline,shadow)*(1.0-fill);
        outColor.rgb*=fill/max(a,0.0001);
    }
    outColor.a*=a;
}
