#version 450
#include "common.glsl"
layout(location=0) out vec3 position;
layout(location=1) out vec2 uv;
layout(location=2) out float particle;
float random(float x){return fract(sin(x*12.9898+78.233)*43758.5453);}
void main(){
    float vertex=float(gl_VertexID);
    particle=floor(vertex/6.0);
    float corner=vertex-particle*6.0;
    vec2 p=vec2(-1,-1);
    if(corner==1.0||corner==4.0)p=vec2(1,-1);
    if(corner==2.0||corner==3.0)p=vec2(-1,1);
    if(corner==5.0)p=vec2(1,1);
    uv=p*.5+.5;
    vec3 center=vec3(actor0.w,actor1.w,actor2.w);
    vec3 right=vec3(view0.x,view1.x,view2.x);
    vec3 up=vec3(view0.y,view1.y,view2.y);
    vec2 offset=p*lighting.x;
    if(lighting.z>1.5&&lighting.z<2.5){
        float angle=particle*6.2831853/16.0+random(particle)*.14;
        vec2 along=vec2(cos(angle),sin(angle));
        offset=along*(uv.x*lighting.x)+vec2(-along.y,along.x)*p.y*lighting.x*.025;
    }
    if(lighting.z>2.5&&lighting.z<3.5){
        vec3 direction=vec3(random(particle*3.0),random(particle*3.0+1.0),random(particle*3.0+2.0))*2.0-1.0;
        center+=direction*lighting.x*(.15+.85*lighting.y);
        offset=p*max(.015,lighting.x*.025)*(1.0-lighting.y);
        float tile=mod(particle,4.0);
        uv=(uv+vec2(mod(tile,2.0),floor(tile*.5)))*.5;
    }
    if(lighting.z>4.5&&lighting.z<5.5)offset=vec2(p.x*lighting.x,p.y*lighting.x*.035);
    position=center+right*offset.x+up*offset.y;
    gl_Position=projectWorld(position);
}
