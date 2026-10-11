#version 450
#include "common.glsl"
layout(binding=0) uniform sampler2D sceneDepth;
layout(binding=1) uniform sampler2D originalPattern;
layout(binding=2) uniform sampler2D originalIce;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec3 tangent;
layout(location=4) in vec3 bitangent;
layout(location=5) in vec3 localPosition;
layout(location=0) out vec4 result;
void main(){
    float depth=dimensions.z+texelFetch(sceneDepth,ivec2(gl_FragCoord.xy),0).r*(dimensions.w-dimensions.z);
    vec3 eye=eyePosition();
    bool waterCover=false;
    if(position.y<transform.w&&eye.y>transform.w+.01){
        float t=(transform.w-eye.y)/(position.y-eye.y);
        float waterDepth=-viewPosition(mix(eye,position,t)).z;
        // Reveal ice only at the water crossing; nearer ground and actors still occlude it.
        waterCover=t>0.0&&t<1.0&&abs(depth-waterDepth)<.08;
    }
    float visible=((-viewPosition(position).z<depth+.08||waterCover)&&position.y>=base.y-.01&&dot(normal,eye-position)>0.0)?1.0:0.0;
    vec3 map=texture(originalIce,uv).xyz*2.0-1.0;
    vec3 n=normalize(tangent*map.x+bitangent*map.y+normal*sqrt(max(0.0,1.0-dot(map.xy,map.xy))));
    vec3 view=normalize(eyePosition()-position);
    float rim=pow(1.0-max(dot(n,view),0.0),3.0);
    vec3 lightDirection=normalize(vec3(-.4,.8,.3));
    float light=.45+.55*max(dot(n,lightDirection),0.0);
    float pattern=texture(originalPattern,uv).r;
    float facing=max(dot(n,view),0.0);
    float frost=smoothstep(.15,.65,length(map.xy));
    float fresnel=.018+.982*pow(1.0-facing,5.0);
    vec3 reflected=reflect(-view,n);
    vec3 sky=mix(vec3(.06,.25,.34),vec3(.61,.88,1.0),smoothstep(-.3,.8,reflected.y));
    vec3 halfway=normalize(view+lightDirection);
    float gloss=mix(150.0,28.0,frost);
    float sparkle=pow(max(dot(n,halfway),0.0),gloss)*(.55+.65*(1.0-frost));
    vec3 interior=vec3(.22,.54,.79)*light;
    float cloud=smoothstep(.12,.72,frost*.65+texture(originalIce,uv*.63+vec2(.17,.31)).b*.35);
    interior=mix(interior,vec3(.62,.73,.88)*(.7+.3*light),cloud*.55);
    vec3 ice=mix(interior,sky,clamp(.14+fresnel*.7,0.0,1.0));
    ice+=vec3(.83,.95,1.0)*sparkle+vec3(.12,.3,.34)*rim;
    ice=mix(ice,vec3(.67,.82,.90)*(.75+.25*light),pattern*.27);
    float alpha=1.0;
    if(base.w>.5){
        rim=pow(1.0-max(dot(normalize(normal),view),0.0),3.0);
        float band=pow(.5+.5*cos(position.y*90.0-flags.x*.15),8.0);
        float grid=pow(.5+.5*cos((localPosition.x+localPosition.z)*80.0),12.0);
        ice=vec3(.27,.8,.94)*(1.0+band*.18+grid*.08)+vec3(.48,.68,.75)*rim;
        alpha=.25+.16*band+.3*rim;
    }
    if(base.w<.5&&flags.w>.5){
        float pulse=.5+.5*sin(flags.x*.12);
        float edge=pow(1.0-max(dot(normalize(normal),view),0.0),2.0);
        ice=mix(ice,vec3(.3,.94,1.0),.48);
        ice+=vec3(.28,.48,.52)*(.45+.12*pulse)+vec3(.55,.85,1.0)*edge;
    }
    float submerged=1.0-smoothstep(transform.w-.035,transform.w+.015,position.y);
    ice=mix(ice,ice*.78+vec3(.035,.085,.095),submerged*.65);
    alpha*=mix(1.0,.62,submerged);
    alpha*=visible;result=vec4(ice*alpha,alpha);
}
