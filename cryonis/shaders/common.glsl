layout(std140,binding=0) uniform Cryonis {
    vec4 dimensions,world0,world1,world2,projection;
    vec4 base,flags,appearance;
    vec4 bones[7];
    vec4 waterOrigin;
    vec4 transform; // cos(yaw), sin(yaw), percentage scale, water surface Y.
};
vec3 rotatePillar(vec3 p){return vec3(transform.x*p.x+transform.y*p.z,p.y,-transform.y*p.x+transform.x*p.z);}
vec3 eyePosition(){return vec3(world0.w,world1.w,world2.w);}
vec3 viewPosition(vec3 p){p-=eyePosition();return vec3(dot(vec3(world0.x,world1.x,world2.x),p),dot(vec3(world0.y,world1.y,world2.y),p),dot(vec3(world0.z,world1.z,world2.z),p));}
vec4 projectWorld(vec3 p){vec3 v=viewPosition(p);return vec4(projection.xy*v.xy+projection.zw*v.z,0,-v.z);}
float hash(float n){return fract(sin(n*127.1+311.7)*43758.5453);}
float symbolLine(vec2 p,vec2 a,vec2 b){vec2 d=b-a;return 1.0-smoothstep(.65,1.45,length(p-a-d*clamp(dot(p-a,d)/dot(d,d),0.0,1.0)));}
float snowflake(vec2 p){float snow=0.0;for(int i=0;i<3;++i){float a=float(i)*1.04719755;vec2 d=vec2(cos(a),sin(a)),v=vec2(-d.y,d.x);
    snow=max(snow,symbolLine(p,-d*11.0,d*11.0));for(int side=-1;side<=1;side+=2){vec2 s=d*float(side);snow=max(snow,symbolLine(p,s*5.0,s*8.0+v*3.5));snow=max(snow,symbolLine(p,s*5.0,s*8.0-v*3.5));}}
    return snow;}
