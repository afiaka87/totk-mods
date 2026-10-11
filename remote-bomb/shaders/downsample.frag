// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#version 450
#include "post.glsl"
layout(binding=0) uniform sampler2D source;
layout(location=0) in vec2 screenUv;
layout(location=0) out vec4 result;
void main(){
    vec2 pixel=floor(screenUv*sizes.zw)*kernel.z;
    vec3 sum=vec3(0);float total=0;
    for(int y=0;y<4;++y)for(int x=0;x<4;++x){
        if(float(x)<kernel.z&&float(y)<kernel.z){
            sum+=texelFetch(source,imagePixel(pixel+vec2(x,y),sizes.xy),0).rgb;
            total+=1;
        }
    }
    result=vec4(sum/total,0);
}
