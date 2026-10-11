// NVN uses the game's framebuffer origin; the GL fixture has a lower-left origin.
ivec2 privatePixel(vec2 p,vec2 size) {
#ifndef STASIS_PREVIEW
    p.y=size.y-1-p.y;
#endif
    return clamp(ivec2(p),ivec2(0),ivec2(size)-1);
}
vec4 privateSample(sampler2D image,vec2 uv,vec2 size) {
    vec2 p=uv*size-.5,f=fract(p);p=floor(p);
    return mix(mix(texelFetch(image,privatePixel(p,size),0),texelFetch(image,privatePixel(p+vec2(1,0),size),0),f.x),
        mix(texelFetch(image,privatePixel(p+vec2(0,1),size),0),texelFetch(image,privatePixel(p+vec2(1,1),size),0),f.x),f.y);
}
