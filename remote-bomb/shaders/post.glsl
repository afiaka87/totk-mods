// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
layout(std140,binding=0) uniform Filter {vec4 sizes,kernel;};
ivec2 imagePixel(vec2 p,vec2 size){
#ifndef REMOTE_BOMB_PREVIEW
    p.y=size.y-1.0-p.y;
#endif
    return clamp(ivec2(p),ivec2(0),ivec2(size)-1);
}
vec4 sampleImage(sampler2D image,vec2 uv,vec2 size){
    vec2 p=uv*size-.5,f=fract(p);p=floor(p);
    return mix(mix(texelFetch(image,imagePixel(p,size),0),texelFetch(image,imagePixel(p+vec2(1,0),size),0),f.x),
               mix(texelFetch(image,imagePixel(p+vec2(0,1),size),0),texelFetch(image,imagePixel(p+vec2(1,1),size),0),f.x),f.y);
}
