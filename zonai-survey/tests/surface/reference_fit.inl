// SPDX-License-Identifier: MIT

inline SfPlane referenceFit(SfSample samples[9], int candidateCount, float fartherSign) {
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
                    float score=sfSupport(samples[0],nx,ny,nz,offset,a,b,c)+sfSupport(samples[1],nx,ny,nz,offset,a,b,c)+
                        sfSupport(samples[2],nx,ny,nz,offset,a,b,c)+sfSupport(samples[3],nx,ny,nz,offset,a,b,c)+
                        sfSupport(samples[4],nx,ny,nz,offset,a,b,c)+sfSupport(samples[5],nx,ny,nz,offset,a,b,c)+
                        sfSupport(samples[6],nx,ny,nz,offset,a,b,c)+sfSupport(samples[7],nx,ny,nz,offset,a,b,c)+
                        sfSupport(samples[8],nx,ny,nz,offset,a,b,c);
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
