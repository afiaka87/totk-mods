#version 450
#include "common.glsl"
layout(binding=0) uniform sampler2D sceneDepth;
layout(binding=3) uniform sampler2D originalCaustic;
layout(location=0) in vec3 position;
layout(location=1) in float coverage;
layout(location=0) out vec4 result;
void main(){
    float depth=dimensions.z+texelFetch(sceneDepth,ivec2(gl_FragCoord.xy),0).r*(dimensions.w-dimensions.z);
    float visible=(coverage>.99&&-viewPosition(position).z<depth+.04)?1.0:0.0;
    if(waterOrigin.w<.5){
        float alpha=snowflake((position.xz-appearance.xz)*45.0)*visible*.96;
        result=vec4(vec3(.87,.98,1.0)*alpha,alpha);
    }else{
    // The .04 m allowance retains coplanar water without depth-difference holes.
    float phase=position.x*95.0+position.z*8.0+flags.x*.03;
    float bands=mix(pow(.5+.5*cos(phase),6.0),.2256,smoothstep(1.0,6.0,abs(dFdx(phase))));
    float sweep=.5+.5*sin(position.z*.18-flags.x*.045);
    float caustic=pow(texture(originalCaustic,fract(position.xz*.08+vec2(flags.x*.0006,flags.x*.0002))).r,6.0);
    vec3 blue=vec3(.065,.46,1.0)+vec3(.025,.08,.11)*bands+vec3(.01,.025,.04)*sweep+vec3(.25,.45,.55)*caustic;
    float fade=1.0-smoothstep(300.0,375.0,distance(position,base.xyz));
    float alpha=.28*visible*fade;result=vec4(blue*alpha,alpha);
    }
}
