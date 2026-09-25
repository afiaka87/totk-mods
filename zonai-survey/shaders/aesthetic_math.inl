// SPDX-License-Identifier: MIT
#ifndef SF_INLINE
#define SF_INLINE
#endif
struct SfSample { float u, v, q, x, y, z, valid; };
struct SfPlane { float a, b, c, nx, ny, nz, offset, score; };
SF_INLINE float sfSmooth(float a, float b, float x) {
    float t = sfClamp((x-a)/(b-a), 0.0, 1.0);
    return t*t*(3.0-2.0*t);
}
SF_INLINE float sfPhase(float radius, float height, float rise) {

    return radius+rise*(sfSqrt(height*height+4.0)-2.0);
}
SF_INLINE float sfSupport(SfSample point,float nx,float ny,float nz,float offset,float a,float b,float c) {
    float result=0.0;
    if (point.valid>0.5) {
        float error=nx*point.x+ny*point.y+nz*point.z-offset;
        float projected=sfAbs(a*point.u+b*point.v+c-point.q)/sfMax(sfAbs(point.q),0.000001);
        result=sfMax(0.0,1.0-sfMax(sfAbs(error)/0.04,projected/0.02));
        if (ny>0.45 && error < -0.04) result-=2.0;
    }
    return result;
}
SF_INLINE SfPlane sfFit(SfSample samples[9], int candidateCount, float fartherSign) {
    SfPlane best;
    best.a=0.0; best.b=0.0; best.c=0.0; best.nx=0.0; best.ny=0.0;
    best.nz=0.0; best.offset=0.0; best.score=0.0;
    for (int candidate=0; candidate<8 && candidate<candidateCount; ++candidate) {
        SfSample p=samples[5], q=samples[6], r=samples[7];
        if (candidate==0) { p=samples[1]; q=samples[2]; r=samples[3]; }
        if (candidate==1) { p=samples[2]; q=samples[3]; r=samples[4]; }
        if (candidate==2) { p=samples[3]; q=samples[4]; r=samples[1]; }
        if (candidate==3) { p=samples[4]; q=samples[1]; r=samples[2]; }
        if (candidate==5) { p=samples[6]; q=samples[7]; r=samples[8]; }
        if (candidate==6) { p=samples[7]; q=samples[8]; r=samples[5]; }
        if (candidate==7) { p=samples[8]; q=samples[5]; r=samples[6]; }
        if (p.valid>0.5 && q.valid>0.5 && r.valid>0.5) {
            float ux=q.x-p.x, uy=q.y-p.y, uz=q.z-p.z;
            float vx=r.x-p.x, vy=r.y-p.y, vz=r.z-p.z;
            float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
            float length=sfSqrt(nx*nx+ny*ny+nz*nz);
            float determinant=(q.u-p.u)*(r.v-p.v)-(r.u-p.u)*(q.v-p.v);
            if (length>0.000001 && sfAbs(determinant)>0.000001) {
                float orient=ny<0.0 ? -1.0 : 1.0;
                nx=orient*nx/length; ny=orient*ny/length; nz=orient*nz/length;
                float offset=nx*p.x+ny*p.y+nz*p.z;
                float residual=nx*samples[0].x+ny*samples[0].y+nz*samples[0].z-offset;
                float tolerance=ny>0.45 ? 1.25 : 0.20;
                if (sfAbs(residual)<=tolerance) {
                    float a=((q.q-p.q)*(r.v-p.v)-(r.q-p.q)*(q.v-p.v))/determinant;
                    float b=((q.u-p.u)*(r.q-p.q)-(r.u-p.u)*(q.q-p.q))/determinant;
                    float c=p.q-a*p.u-b*p.v;
                    float threshold=sfMax(5.25,best.score-0.01);
                    float score=sfSupport(samples[0],nx,ny,nz,offset,a,b,c)+sfSupport(samples[1],nx,ny,nz,offset,a,b,c)+
                        sfSupport(samples[2],nx,ny,nz,offset,a,b,c);

                    if (score+6.001>=threshold) {
                        score+=sfSupport(samples[3],nx,ny,nz,offset,a,b,c);
                        score+=sfSupport(samples[4],nx,ny,nz,offset,a,b,c);
                        score+=sfSupport(samples[5],nx,ny,nz,offset,a,b,c);
                        if (score+3.001>=threshold) {
                            score+=sfSupport(samples[6],nx,ny,nz,offset,a,b,c);
                            score+=sfSupport(samples[7],nx,ny,nz,offset,a,b,c);
                            score+=sfSupport(samples[8],nx,ny,nz,offset,a,b,c);
                        } else score=-100.0;
                    } else score=-100.0;
                    float depthDifference=fartherSign*(c-best.c);
                    float depthTolerance=sfMax(sfAbs(best.c),0.000001)*0.0001;
                    bool farther=depthDifference>depthTolerance ||
                        (sfAbs(depthDifference)<=depthTolerance && ny>best.ny);
                    bool better=score>best.score+0.01 || (sfAbs(score-best.score)<=0.01 && farther);
                    if (c>0.0 && score>=5.25 && better) {
                        best.a=a; best.b=b; best.c=c;
                        best.nx=nx; best.ny=ny; best.nz=nz; best.offset=offset; best.score=score;
                    }
                }
            }
        }
    }
    return best;
}
SF_INLINE float sfTrailIntegral(float x, float length) {
    float t=sfClamp(x/length, 0.0, 1.0);
    return length*(t-t*t+t*t*t/3.0);
}
SF_INLINE float sfCoverage(float distance, float halfWidth, float footprint, float trail) {
    float span=sfMax(0.001, footprint);
    float width=sfMax(halfWidth, 0.65*span);
    float lo=sfMax(distance-span*0.5, -width);
    float hi=sfMin(distance+span*0.5, width);
    float core=sfClamp((hi-lo)/span, 0.0, 1.0);
    float halo=0.12*(1.0-sfSmooth(width, width+span*1.25, sfAbs(distance)));
    float tail=0.0;
    if (trail>0.001)
        tail=0.48*(sfTrailIntegral(-distance+span*0.5,trail)-
                   sfTrailIntegral(-distance-span*0.5,trail))/span;
    return sfClamp(core+halo+tail, 0.0, 1.0);
}
SF_INLINE float sfDensity(float footprint, float spacing) {
    return 1.0-sfSmooth(spacing*0.4, spacing*0.9, footprint);
}
SF_INLINE float sfRingCoordinate(float radius) {
    float coordinate=radius/0.625;
    if (radius>2.5) coordinate=4.0+(radius-2.5)/2.5;
    if (radius>25.0)
        coordinate=13.0+sfLog2(1.0+(radius-25.0)*(0.135/2.8375))/sfLog2(1.135);
    return coordinate;
}
SF_INLINE float sfRingRadius(float coordinate) {
    float radius=coordinate*0.625;
    if (coordinate>4.0) radius=2.5+(coordinate-4.0)*2.5;
    if (coordinate>13.0)
        radius=25.0+(2.8375/0.135)*(sfExp2((coordinate-13.0)*sfLog2(1.135))-1.0);
    return radius;
}
SF_INLINE float sfStationaryRing(float radius,float footprint,float width) {
    float index=sfFloor(sfRingCoordinate(radius)+0.5);
    float center=sfRingRadius(index);
    float spacing=sfMin(center-sfRingRadius(index-1.0),sfRingRadius(index+1.0)-center);
    float result=sfCoverage(radius-center,width,footprint,0.0)*sfDensity(footprint,spacing);
    if (index<1.0 || index>37.0) result=0.0;
    return result;
}
SF_INLINE float sfGridLine(float coordinate,float spacing,float footprint,float width) {
    float distance=coordinate-spacing*sfFloor(coordinate/spacing+0.5);
    return sfCoverage(distance,width,footprint,0.0)*sfDensity(footprint,spacing);
}
SF_INLINE float sfHeightContour(float height,float spacing,float footprint,float width,float up) {

    float slope=sfSqrt(sfMax(0.0,1.0-up*up));
    return sfGridLine(height,spacing,footprint,width*slope);
}
SF_INLINE float sfImprintEnvelope(float forward,float height,float front) {
    float reveal=sfRingCoordinate(sfMax(0.0,forward))+sfMin(2.0,sfPhase(0.0,height,1.0)/12.0);
    float behind=front-reveal;

    float result=sfSmooth(0.0,0.35,behind)*(1.0-sfSmooth(0.35,10.0,behind));
    if (front<0.0) result=1.0;
    return result;
}
SF_INLINE float sfGridConfidence(float support) { return sfSmooth(7.0,8.5,support); }
struct SfRange { float lo,hi; };
SF_INLINE SfRange sfFitQBounds(SfSample samples[9],float stride) {
    SfRange q; q.lo=samples[0].q; q.hi=q.lo;
    for (int i=1;i<9;++i) if (samples[i].valid>0.5) {
        q.lo=sfMin(q.lo,samples[i].q); q.hi=sfMax(q.hi,samples[i].q);
    }

    // Include subpixel extrapolation so the work gate cannot reject a visible fit.
    float pad=2.0*(q.hi-q.lo)/sfMax(stride,1.0)+sfMax(q.hi*0.0001,0.000001);
    q.lo=sfMax(0.000001,q.lo-pad); q.hi+=pad;
    return q;
}
SF_INLINE bool sfMayImprintRange(float lo,float hi,float front) {

    float first=sfRingCoordinate(sfMax(0.0,lo));
    float last=sfRingCoordinate(sfMax(0.0,hi))+12.0;
    return front<0.0 || (front>=first-0.05 && front<=last+0.05);
}
SF_INLINE float sfForward(float x,float z,float headingX,float headingZ) { return x*headingX+z*headingZ; }
struct SfPaint { float r,g,b,a; };
SF_INLINE SfPaint sfImprint(float x,float y,float z,float nx,float ny,float nz,
                            float fx,float fy,float fz,float forward,float forwardFootprint,
                            float front,float spacing,float width,float coherence) {
    float radius=sfSqrt(x*x+z*z);
    float up=sfAbs(ny), ground=sfSmooth(0.5,0.85,up);

    // Height is relative to the scan origin; this is not a grass material test.
    float low=1.0-sfSmooth(1.25,2.25,sfAbs(y));

    float regular=1.0-low;
    float weightX=sfAbs(nz)/sfMax(sfAbs(nx)+sfAbs(nz),0.000001);
    float vertical=sfGridLine(x,spacing,fx,width)*weightX+
                   sfGridLine(z,spacing,fz,width)*(1.0-weightX);
    float row=sfStationaryRing(forward,forwardFootprint,width);

    float horizontal=sfHeightContour(y,spacing,fy,width,up);
    float grid=sfMax(horizontal*(0.32+0.68*coherence),vertical*0.85*coherence);
    float obstacle=low*row*0.30+(1.0-low)*grid;
    float line=row*ground+obstacle*(1.0-ground);
    float warm=(1.0-sfSmooth(0.68,0.92,up))*regular;
    float cliff=(1.0-sfSmooth(0.20,0.64,up))*regular;
    SfPaint p;
    p.r=0.10+0.90*warm;
    p.g=(0.78+0.07*warm)*(1.0-cliff)+0.20*cliff;
    p.b=(1.0-0.92*warm)*(1.0-cliff)+0.08*cliff;
    p.a=line*sfImprintEnvelope(forward,y,front)*(1.0-sfSmooth(420.0,450.0,radius));
    return p;
}
