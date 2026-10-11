layout(std140,binding=0) uniform Stasis {
    vec4 dimensions,target,tail,tip,flags,timing,tint,extra;
    vec4 world0,world1,world2,projection,selectedMin,selectedMax;
    vec4 availableMin[16],availableMax[16];
    vec4 chains[6],maskDimensions;
    vec4 effects,pipeline,resources,trailParams;
    vec4 trail[96];
};
vec3 eyePosition() { return vec3(world0.w,world1.w,world2.w); }
vec3 viewPosition(vec3 p) {
    p-=eyePosition();
    return vec3(dot(vec3(world0.x,world1.x,world2.x),p),
        dot(vec3(world0.y,world1.y,world2.y),p),dot(vec3(world0.z,world1.z,world2.z),p));
}
vec4 projectWorld(vec3 p) {
    vec3 v=viewPosition(p);
    return vec4(projection.xy*v.xy+projection.zw*v.z,0,-v.z);
}
vec3 worldAt(vec2 uv,float z) {
    vec4 v=vec4((uv*2-1+projection.zw)*z/projection.xy,-z,1);
    return vec3(dot(world0,v),dot(world1,v),dot(world2,v));
}
vec3 centrePosition() { return (selectedMin.xyz+selectedMax.xyz)*.5; }
