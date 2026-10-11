layout(std140,binding=0) uniform Bomb {
    vec4 dimensions,view0,view1,view2,projection;
    vec4 actor0,actor1,actor2,lighting,emission,highlight,outline;
};
vec3 eyePosition(){return vec3(view0.w,view1.w,view2.w);}
vec3 viewPosition(vec3 p){
    p-=eyePosition();
    return vec3(dot(vec3(view0.x,view1.x,view2.x),p),
                dot(vec3(view0.y,view1.y,view2.y),p),
                dot(vec3(view0.z,view1.z,view2.z),p));
}
vec3 rotateBomb(vec3 p){return vec3(dot(actor0.xyz,p),dot(actor1.xyz,p),dot(actor2.xyz,p));}
vec3 worldPosition(vec3 p){return rotateBomb(p)+vec3(actor0.w,actor1.w,actor2.w);}
vec4 projectWorld(vec3 p){
    vec3 v=viewPosition(p);
    return vec4(projection.xy*v.xy+projection.zw*v.z,0,-v.z);
}
