// SPDX-License-Identifier: MIT
#version 450
#ifndef SF_DIAGNOSTICS
#define SF_DIAGNOSTICS 1
#endif
#ifndef SF_CONSTRAINED
#define SF_CONSTRAINED 0
#endif
layout(location=0) in vec2 screenUv;
layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D sceneDepth;
layout(std140,binding=0) uniform Diagnostic {
    vec4 inverseProjection[4];
    vec4 inverseView[3];
    vec4 settings;
    vec4 dimensions;
    vec4 scanOrigin;
    vec4 scanHeading;
    vec4 scanStyle;
    vec4 surfaceStyle;
    vec4 scanMotion;
};
float sfAbs(float x) { return abs(x); }
float sfSqrt(float x) { return sqrt(x); }
float sfMin(float a,float b) { return min(a,b); }
float sfMax(float a,float b) { return max(a,b); }
float sfClamp(float x,float a,float b) { return clamp(x,a,b); }
float sfFloor(float x) { return floor(x); }
float sfLog2(float x) { return log2(x); }
float sfExp2(float x) { return exp2(x); }
// @include aesthetic_math.inl
vec4 unproject(vec4 p) {
    return vec4(dot(inverseProjection[0],p),dot(inverseProjection[1],p),
                dot(inverseProjection[2],p),dot(inverseProjection[3],p));
}
bool validDepth(float d) { return !isnan(d) && !isinf(d) && d>=0.0 && d<1.0; }
vec2 pixelUv(vec2 pixel) {
    vec2 uv=(pixel+vec2(0.5))/dimensions.xy;
    return vec2(uv.x,1.0-uv.y);
}
vec3 viewAt(vec2 uv,float distance) {
    vec4 p=unproject(vec4(uv*2.0-1.0,-1.0,1.0));
    vec3 ray=p.xyz/p.w;
    float scale=settings.z>0.5 ? distance/-ray.z : 1.0;
    return vec3(ray.xy*scale,-distance);
}
vec3 worldAt(vec2 uv,float distance) {
    vec3 p=viewAt(uv,distance);
    return vec3(dot(inverseView[0],vec4(p,1.0)),dot(inverseView[1],vec4(p,1.0)),
                dot(inverseView[2],vec4(p,1.0)));
}
float radiusAt(vec3 p) {
    vec3 delta=p-scanOrigin.xyz;
    return sfPhase(length(delta.xz),delta.y,scanMotion.w);
}
SfSample sampleAt(ivec2 pixel,vec2 offset) {
    SfSample s;
    s.u=offset.x; s.v=offset.y; s.q=0.0; s.x=0.0; s.y=0.0; s.z=0.0; s.valid=0.0;
    if (all(greaterThanEqual(pixel,ivec2(0))) && all(lessThan(pixel,ivec2(dimensions.xy)))) {
        float d=texelFetch(sceneDepth,pixel,0).r;
        if (validDepth(d)) {
            float distance=dimensions.z+d*(dimensions.w-dimensions.z);
            vec3 world=worldAt(pixelUv(vec2(pixel)),distance);
            if (!any(isnan(world)) && !any(isinf(world))) {
                s.q=settings.z>0.5 ? 1.0/distance : distance;
                s.x=world.x; s.y=world.y; s.z=world.z; s.valid=1.0;
            }
        }
    }
    return s;
}
vec3 planeWorld(SfPlane plane,vec2 pixel,vec2 offset) {
    float q=plane.c+plane.a*offset.x+plane.b*offset.y;
    float distance=settings.z>0.5 ? 1.0/max(q,0.000001) : max(q,0.001);
    return worldAt(pixelUv(pixel+offset),distance);
}
vec3 imprintAt(vec3 world,vec3 dx,vec3 dy,vec3 normal,float coherence) {
    vec3 p=world-scanOrigin.xyz, footprint=abs(dx)+abs(dy);
    float forward=sfForward(p.x,p.z,scanHeading.x,scanHeading.y);
    float forwardFootprint=abs(dot(dx.xz,scanHeading.xy))+abs(dot(dy.xz,scanHeading.xy));
    SfPaint paint=sfImprint(p.x,p.y,p.z,normal.x,normal.y,normal.z,
        footprint.x,footprint.y,footprint.z,forward,max(forwardFootprint,0.025),
        surfaceStyle.x,surfaceStyle.z,surfaceStyle.w,coherence);
    vec3 color=vec3(paint.r,paint.g,paint.b)*paint.a;
#if SF_CONSTRAINED
    color*=1.0-smoothstep(settings.y-min(8.0,settings.y*0.05),settings.y,length(p.xz));
#endif
    return color;
}
vec3 localAxis(SfSample center,SfSample before,SfSample after,float stride) {
    vec3 p=vec3(center.x,center.y,center.z);
    vec3 lo=p-vec3(before.x,before.y,before.z), hi=vec3(after.x,after.y,after.z)-p;
    vec3 result=vec3(0);
    if (before.valid>0.5) result=lo;
    if (after.valid>0.5 && (before.valid<0.5 || dot(hi,hi)<dot(lo,lo))) result=hi;
    return result/stride;
}
float bands(float radius,float footprint,float direction) {
    float result=0.0;
    for (int i=0; i<8; ++i) {
        float center=scanHeading.w-float(i)*scanStyle.x;
        if (center>0.0 && center<=450.0)
            result=max(result,sfCoverage(radius-center,scanStyle.z,footprint,scanMotion.x));
    }
    return result*sfDensity(footprint,scanStyle.x)*
           (1.0-smoothstep(420.0,450.0,radius))*smoothstep(scanHeading.z,scanHeading.z+0.015,direction);
}
void main() {
    vec2 texel=vec2(screenUv.x,1.0-screenUv.y)*dimensions.xy;
    ivec2 pixel=clamp(ivec2(texel),ivec2(0),ivec2(dimensions.xy)-ivec2(1));
    SfSample center=sampleAt(pixel,vec2(0.0));
    vec3 world=vec3(center.x,center.y,center.z);

    if (settings.w<0.5) {
        outColor=vec4(0.7,0.12,0.0,1.0);
    } else if (center.valid<0.5) {
        if (SF_DIAGNOSTICS!=0 && settings.x<2.5) outColor=vec4(0.08,0.04,0.18,1.0);
        else discard;
    } else if (SF_DIAGNOSTICS!=0 && settings.x<2.5) {
        float distance=settings.z>0.5 ? 1.0/center.q : center.q;
        float shade=screenUv.x<0.5 ? clamp(distance/100.0,0.0,1.0) : fract(distance/20.0);
        outColor=vec4(vec3(shade),1.0);
    } else {
        float groundRadius=length((world-scanOrigin.xyz).xz);
        float direction=dot((world-scanOrigin.xyz).xz,scanHeading.xy)/max(groundRadius,0.001);
        float maxRadius=470.0;
#if SF_CONSTRAINED
        maxRadius=settings.y;
#endif
        if (scanOrigin.w<0.5 || groundRadius>maxRadius || direction<scanHeading.z-0.02) {
            discard;
        } else {
            SfSample samples[9];
            samples[0]=center;
            int step=int(scanMotion.z);
            float stride=scanMotion.z;
            samples[1]=sampleAt(pixel+ivec2(-step,-step),vec2(-stride,-stride));
            samples[2]=sampleAt(pixel+ivec2(step,-step),vec2(stride,-stride));
            samples[3]=sampleAt(pixel+ivec2(step,step),vec2(stride,stride));
            samples[4]=sampleAt(pixel+ivec2(-step,step),vec2(-stride,stride));
            samples[5]=sampleAt(pixel+ivec2(-step,0),vec2(-stride,0));
            samples[6]=sampleAt(pixel+ivec2(0,-step),vec2(0,-stride));
            samples[7]=sampleAt(pixel+ivec2(step,0),vec2(stride,0));
            samples[8]=sampleAt(pixel+ivec2(0,step),vec2(0,stride));
            vec2 subpixel=texel-(vec2(pixel)+vec2(0.5));
            bool mayDraw=true;
            if ((SF_DIAGNOSTICS==0 || surfaceStyle.y>0.5) && surfaceStyle.x>=0.0) {
                SfRange q=sfFitQBounds(samples,stride);
                float nearDistance=settings.z>0.5 ? 1.0/q.hi : max(q.lo,0.001);
                float farDistance=settings.z>0.5 ? 1.0/q.lo : max(q.hi,0.001);
                vec2 uv=pixelUv(vec2(pixel)+subpixel);
                vec3 nearWorld=worldAt(uv,nearDistance)-scanOrigin.xyz;
                vec3 farWorld=worldAt(uv,farDistance)-scanOrigin.xyz;
                float nearForward=dot(nearWorld.xz,scanHeading.xy);
                float farForward=dot(farWorld.xz,scanHeading.xy);
                float rawForward=dot((world-scanOrigin.xyz).xz,scanHeading.xy);
                float lo=min(rawForward,min(nearForward,farForward));
                float hi=max(rawForward,max(nearForward,farForward));
                mayDraw=sfMayImprintRange(lo,hi,surfaceStyle.x);
            }
            if (!mayDraw) {
                discard;
            } else {
                vec3 rawDx=localAxis(center,samples[5],samples[7],stride);
                vec3 rawDy=localAxis(center,samples[6],samples[8],stride);
                SfPlane plane=sfFit(samples,int(scanStyle.y),settings.z>0.5 ? -1.0 : 1.0);
                float rawLine=0.0;
                if (SF_DIAGNOSTICS!=0 && surfaceStyle.y<0.5) {
                    float distance=settings.z>0.5 ? 1.0/center.q : center.q;
                    float rawRadius=radiusAt(world);
                    float gx=radiusAt(worldAt(pixelUv(vec2(pixel)+vec2(1,0)),distance))-rawRadius;
                    float gy=radiusAt(worldAt(pixelUv(vec2(pixel)+vec2(0,1)),distance))-rawRadius;
                    rawLine=bands(rawRadius,clamp(abs(gx)+abs(gy),0.025,1.0),direction);
                }
                float line=rawLine;
                vec3 normal=cross(rawDx,rawDy);
                float normalLength=length(normal);
                normal=normalLength>0.000001 ? normal/normalLength : vec3(0,1,0);
                vec3 fittedWorld=world, fittedDx=rawDx, fittedDy=rawDy;
                if (plane.score>0.0) {
                    fittedWorld=planeWorld(plane,vec2(pixel),subpixel);
                    vec3 xp=planeWorld(plane,vec2(pixel),subpixel+vec2(0.5,0));
                    vec3 xm=planeWorld(plane,vec2(pixel),subpixel-vec2(0.5,0));
                    vec3 yp=planeWorld(plane,vec2(pixel),subpixel+vec2(0,0.5));
                    vec3 ym=planeWorld(plane,vec2(pixel),subpixel-vec2(0,0.5));
                    fittedDx=xp-xm; fittedDy=yp-ym;
                    normal=vec3(plane.nx,plane.ny,plane.nz);
                    if (SF_DIAGNOSTICS!=0 && surfaceStyle.y<0.5) {
                        float dx=radiusAt(xp)-radiusAt(xm), dy=radiusAt(yp)-radiusAt(ym);
                        line=max(bands(radiusAt(fittedWorld),max(abs(dx)+abs(dy),0.025),direction),rawLine*scanMotion.y);
                    }
                }
                vec3 radiance=vec3(0.10,0.78,1.0)*line;
                if (SF_DIAGNOSTICS==0 || surfaceStyle.y>0.5) {
                    radiance=imprintAt(fittedWorld,fittedDx,fittedDy,normal,sfGridConfidence(plane.score));
                    float residual=dot(normal,world)-plane.offset;
                    if (plane.score>0.0 && abs(residual)>0.04) {

                        vec3 p=world-scanOrigin.xyz;
                        float forward=sfForward(p.x,p.z,scanHeading.x,scanHeading.y);
                        float footprint=abs(dot(rawDx.xz,scanHeading.xy))+abs(dot(rawDy.xz,scanHeading.xy));
                        float detail=sfStationaryRing(forward,max(footprint,0.025),surfaceStyle.w)*
                            sfImprintEnvelope(forward,world.y-scanOrigin.y,surfaceStyle.x)*scanMotion.y;
                        detail*=1.0-smoothstep(420.0,450.0,groundRadius);
#if SF_CONSTRAINED
                        detail*=1.0-smoothstep(settings.y-min(8.0,settings.y*0.05),settings.y,groundRadius);
#endif
                        radiance=max(radiance,vec3(0.10,0.78,1.0)*detail);
                    }
                    radiance*=smoothstep(scanHeading.z,scanHeading.z+0.015,direction);
                }
                if (max(radiance.r,max(radiance.g,radiance.b))*scanStyle.w<0.005) discard;
                outColor=vec4(radiance,scanStyle.w);
            }
        }
    }
}
