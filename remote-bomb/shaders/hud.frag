#version 450 core
layout(std140,binding=0) uniform Hud { vec4 rect,gauge,tint; };
layout(binding=0) uniform sampler2D rune;
layout(binding=1) uniform sampler2D ticks;
layout(binding=2) uniform sampler2D glow;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
void main(){
    vec2 p=uv*2-1;
    vec2 iconUV=(uv-.5)*gauge.z+.5;
    bool inside=all(greaterThanEqual(iconUV,vec2(0)))&&all(lessThanEqual(iconUV,vec2(1)));
    vec4 icon=texture(rune,clamp(iconUV,vec2(.0001),vec2(.9999)));
    float iconAlpha=inside?icon.a:0;
    bool champion=gauge.w<0;
    if(champion){
        float capacity=floor((-gauge.w-1)/8),charges=(-gauge.w-1)-capacity*8;
        for(int i=0;i<6;++i){
            float point=float(i);
            if(point>=capacity) break;
            vec2 centre=vec2((point-(capacity-1)*.5)*.21,.75);
            float pip=(1-smoothstep(.055,.085,length(p-centre)))*(point<charges?1:.22);
            iconAlpha=max(iconAlpha,pip);
        }
    }
    float angle=fract(atan(p.x,-p.y)/6.28318530718+1);
    float edge=.004;
    float arc=gauge.x>=1?1:(gauge.x<=0?0:1-smoothstep(gauge.x-edge,gauge.x+edge,angle));
    vec2 ringUV=p.x<0?1.0-uv:uv;
    float ring=texture(ticks,ringUV).a*gauge.y*arc*(200.0/255.0);
    float halo=texture(glow,ringUV).a*gauge.y*arc*.32;
    float iconOpacity=mix(1.0,.62,gauge.y);
    float a=iconAlpha*iconOpacity;
    vec3 rgb=(champion?vec3(1):icon.rgb)*tint.rgb*a;
    float overlay=min(1,ring+halo);
    rgb=rgb*(1-overlay)+vec3(.60,.86,1)*overlay;
    a=a*(1-overlay)+overlay;
    color=vec4(rgb/max(a,.0001),a*tint.a);
}
