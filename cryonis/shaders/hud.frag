#version 450
#include "common.glsl"
layout(location=0) out vec4 result;
float line(vec2 p,vec2 a,vec2 b){vec2 d=b-a;return 1.0-smoothstep(.65,1.45,length(p-a-d*clamp(dot(p-a,d)/dot(d,d),0.0,1.0)));}
void main(){
    vec2 pixel=vec2(gl_FragCoord.x,dimensions.y-gl_FragCoord.y)*720.0/dimensions.y;
    vec4 anchor=projectWorld(base.xyz+vec3(0,1.3,0));
    vec2 centre=(anchor.xy/max(anchor.w,.001)*.5+.5)*dimensions.xy*720.0/dimensions.y+vec2(-45,-35);
    vec2 p=pixel-centre;float radius=length(p),angle=atan(p.x,p.y);if(angle<0.0)angle+=6.2831853;
    float snow=0.0;
    for(int i=0;i<3;++i){float a=float(i)*1.04719755;vec2 d=vec2(cos(a),sin(a)),v=vec2(-d.y,d.x);
        snow=max(snow,line(p,-d*11.0,d*11.0));
        for(int side=-1;side<=1;side+=2){vec2 s=d*float(side);snow=max(snow,line(p,s*5.0,s*8.0+v*3.5));snow=max(snow,line(p,s*5.0,s*8.0-v*3.5));}
    }
    float ring=(1.0-smoothstep(1.1,2.1,abs(radius-22.0)))*step(angle/6.2831853,1.0-flags.z/50.0);
    ring*=pow(.5+.5*cos(angle*64.0),2.0);
    float shade=(1.0-smoothstep(24.0,28.0,radius))*.4;
    float alpha=max(shade,max(snow,ring));vec3 color=vec3(.33,.85,1.0)*max(snow,ring);
    alpha*=step(.001,flags.z)*step(.001,anchor.w);color*=step(.001,flags.z)*step(.001,anchor.w);
    if(base.w>.5){vec2 q=pixel-vec2(dimensions.x/dimensions.y*720.0*.595,140.0);
        float disc=1.0-smoothstep(14.0,15.0,length(q));alpha=max(alpha,disc*.9);color=mix(color,vec3(.88,.97,1.0)*disc,disc);
        float letter=max(line(q,vec2(-5,-6),vec2(0,7)),max(line(q,vec2(0,7),vec2(5,-6)),line(q,vec2(-3.2,-1.5),vec2(3.2,-1.5))));
        color*=1.0-letter*disc*.9;
    }
    result=vec4(color,alpha);
}
