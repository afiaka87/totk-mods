#version 450
#include "common.glsl"
layout(location=0) out vec3 position;
layout(location=1) out vec2 local;
layout(location=2) out float opacity;
layout(location=3) flat out float kind;
void main(){
    float index=floor(float(gl_VertexID)/6.0);int corner=gl_VertexID-int(index)*6;
    local=vec2(corner==1||corner==2||corner==4?1:-1,corner==2||corner==4||corner==5?1:-1);
    kind=appearance.w;vec3 centre=base.xyz;float size=.16;
    float r=hash(index+3.0),s=hash(index+19.0),t=hash(index+43.0);
    if(kind<.5){
        centre+=rotatePillar(vec3((r<.5?-1.51:1.51)*transform.z,appearance.z*s,(t<.5?-1.51:1.51)*transform.z));
        float phase=fract(flags.x*.017+hash(index+61.0));
        opacity=pow(max(0.0,sin(phase*3.14159265)),18.0)*.85;
        size=.12+.12*hash(index+28.0);
    }else if(kind<1.5){
        float age=fract(flags.x*.033+index*.6180339);
        centre+=rotatePillar(vec3((r-.5)*(3.3+age)*transform.z,appearance.z+(.2+age*.8)*transform.z,(t-.5)*(3.3+age)*transform.z));
        size=.045+.07*s;opacity=(1.0-age)*smoothstep(0.0,6.0,flags.y)*(1.0-smoothstep(80.0,100.0,flags.y));
    }else{
        float age=appearance.x/35.0;
        centre+=rotatePillar(vec3((r-.5)*(3.0+age*4.0),s*4.0+age*2.0-age*age*5.0,(t-.5)*(3.0+age*4.0))*transform.z);
        size=.04+.11*hash(index+28.0);opacity=(1.0-smoothstep(.2,1.0,age))*step(0.0,age);
    }
    size*=transform.z;
    position=centre+vec3(world0.x,world1.x,world2.x)*local.x*size+vec3(world0.y,world1.y,world2.y)*local.y*size;
    gl_Position=projectWorld(position);
}
