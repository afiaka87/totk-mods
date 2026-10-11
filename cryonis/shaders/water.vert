#version 450
#include "common.glsl"
layout(std140,binding=2) uniform WaterGrid {vec4 water[1024];};
layout(location=0) out vec3 position;
layout(location=1) out float coverage;
float height(float x,float z){
    vec4 h=water[int(floor((x+z*64.0)*.25))];float lane=mod(x,4.0);
    return lane<1.0?h.x:lane<2.0?h.y:lane<3.0?h.z:h.w;
}
float coordinate(float index){
    float t=index-31.5,tail=max(abs(t)-15.5,0.0)/16.0;
    return 2.0*t+sign(t)*(waterOrigin.z-63.0)*tail*tail*tail*tail;
}
void main(){
    float id=float(gl_VertexID),quad=floor(id/6.0),v=id-quad*6.0;
    if(waterOrigin.w<.5){
        float dx=(v>0.5&&v<2.5)||(v>3.5&&v<4.5)?1.0:0.0;
        float dz=(v>1.5&&v<2.5)||v>3.5?1.0:0.0;
        position=appearance.xyz+vec3((dx-.5)*.8,.035,(dz-.5)*.8);
        coverage=1.0;gl_Position=projectWorld(position);
    }else{
    float x=mod(quad,63.0),z=floor(quad/63.0);
    float a=height(x,z),b=height(x+1.0,z),c=height(x+1.0,z+1.0),d=height(x,z+1.0);
    float dx=(v>0.5&&v<2.5)||(v>3.5&&v<4.5)?1.0:0.0;
    float dz=(v>1.5&&v<2.5)||v>3.5?1.0:0.0;
    vec4 wet=vec4(a<10000.0?1:0,b<10000.0?1:0,c<10000.0?1:0,d<10000.0?1:0);
    float count=dot(wet,vec4(1)),edge=dot(vec4(a,b,c,d),wet)/max(count,1.0);
    vec4 lows=mix(vec4(10000),vec4(a,b,c,d),wet),highs=mix(vec4(-10000),vec4(a,b,c,d),wet);
    float span=max(max(highs.x,highs.y),max(highs.z,highs.w))-min(min(lows.x,lows.y),min(lows.z,lows.w));
    // Wet cell heights fill the shore; native depth clips the coastline per pixel.
    vec4 heights=mix(vec4(edge),vec4(a,b,c,d),wet);
    float y=dx<.5?(dz<.5?heights.x:heights.w):(dz<.5?heights.y:heights.z);
    coverage=count>0.0&&span<=.1?1.0:0.0;
    // Lift the mesh 2.5 cm above the surface: native water can occupy scene depth.
    position=vec3(waterOrigin.x+coordinate(x+dx),y+.025,waterOrigin.y+coordinate(z+dz));
    gl_Position=coverage>.5?projectWorld(position):vec4(2,2,0,1);
    }
}
