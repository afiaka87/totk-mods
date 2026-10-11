layout(binding=0) uniform sampler2D sceneDepth;
ivec2 pixelAt(vec2 uv,ivec2 size) {
    return clamp(ivec2(vec2(uv.x,1-uv.y)*vec2(size)),ivec2(0),size-ivec2(1));
}
float sceneMetres(vec2 uv) {
    float d=texelFetch(sceneDepth,pixelAt(uv,ivec2(dimensions.xy)),0).r;
    if(isnan(d)||isinf(d)||d<0||d>=1) d=1;
    return dimensions.z+d*(dimensions.w-dimensions.z);
}
float visibleWorld(vec3 p) {
    vec4 clip=projectWorld(p);
    vec2 uv=clip.xy/max(clip.w,.0001)*.5+.5;
    return step(dimensions.z,clip.w)*step(clip.w-.16,sceneMetres(uv));
}
