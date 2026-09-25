// SPDX-License-Identifier: MIT
#include <doctest.h>
#include <cstdio>
#include "FidelityPolicy.hpp"
#include "../../src/pure/PulseLattice.hpp"
using namespace survey_fidelity;
namespace {
#include "reference_fit.inl"
constexpr float offsets[9][2]{{0,0},{-6,-6},{6,-6},{6,6},{-6,6},{-6,0},{0,-6},{6,0},{0,6}};
SfSample groundSample(int i,float range,float height=0) {
    const float u=offsets[i][0],v=offsets[i][1],rayY=-2.f/range+v/800.f;
    if (rayY>=0) return {u,v,0,0,0,0,0};
    const float t=(height-2.f)/rayY;
    return {u,v,1.f/t,u*t/800.f,height,t,1};
}
void ground(SfSample samples[9],float range) {
    for (int i=0;i<9;++i) samples[i]=groundSample(i,range);
}
}
TEST_CASE("actual shader fitting math recovers grazing ground beyond the old distance cutoff") {
    for (float range : {20.f,100.f,200.f,300.f,450.f}) {
        SfSample s[9]; ground(s,range);
        const auto p=sfFit(s,8);
        REQUIRE(p.score>=4.25f);
        CHECK(p.ny == doctest::Approx(1));
        CHECK(1.f/p.c == doctest::Approx(range));
        for (float x : {-0.5f,0.f,0.5f}) for (float y : {-0.5f,0.f,0.5f}) {
            const float expected=2.f/(2.f/range-y/800.f);
            CHECK(1.f/(p.c+p.a*x+p.b*y) == doctest::Approx(expected));
        }
        const float jump=range-1600.f/(1600.f/range+1.f);
        if (range>=100) CHECK(jump>std::min(8.f,range*.02f));
    }
}
TEST_CASE("actual shader fit ignores sparse blade foreground without moving the ground") {
    for (float range : {10.f,20.f,60.f,100.f}) {
        SfSample s[9]; ground(s,range);
        for (int i : {0,2,7}) s[i]=groundSample(i,range,0.8f);
        const auto p=sfFit(s,8);
        REQUIRE(p.score>=4.25f);
        CHECK(1.f/p.c == doctest::Approx(range));
        CHECK(p.ny == doctest::Approx(1));
    }
}
TEST_CASE("invalid sky insufficient support and large occluding objects do not invent a ground plane") {
    SfSample s[9]{};
    CHECK(sfFit(s,8).score==0);
    ground(s,100);
    for (int i=1;i<9;++i) s[i].valid=0;
    CHECK(sfFit(s,8).score==0);
    ground(s,20);
    s[0]=groundSample(0,20,1.6f);
    CHECK(sfFit(s,8).score==0);
}
TEST_CASE("every placement of three sparse grass samples preserves the underlying flat contour") {
    for (float range : {10.f,20.f,60.f,100.f})
        for (int a=0;a<7;++a) for (int b=a+1;b<8;++b) for (int c=b+1;c<9;++c) {
            SfSample s[9]; ground(s,range);
            for (int i : {a,b,c}) s[i]=groundSample(i,range,.8f);
            const auto p=sfFit(s,8);
            CAPTURE(range); CAPTURE(a); CAPTURE(b); CAPTURE(c); CAPTURE(p.score);
            REQUIRE(p.score>=5.25f);
            CHECK(p.ny==doctest::Approx(1));
            CHECK(1.f/p.c==doctest::Approx(range));
        }
}
TEST_CASE("vertical planar walls retain contours and isolated foreground does not borrow a distant wall") {
    SfSample s[9];
    for (int i=0;i<9;++i) {
        float u=offsets[i][0],v=offsets[i][1];
        s[i]={u,v,1.f/20.f,u/40.f,2+v/40.f,20,1};
    }
    const auto p=sfFit(s,8);
    CHECK(p.score>=4.25f); CHECK(std::abs(p.nz)==doctest::Approx(1));
    CHECK(1.f/p.c==doctest::Approx(20));
    s[0].z=10; s[0].q=0.1f;
    CHECK(sfFit(s,8).score==0);
}
TEST_CASE("fit is stable under large world translation and modest terrain slope") {
    SfSample s[9]; ground(s,100);
    for (auto& point:s) { point.y+=point.x*.2f; point.x+=10000; point.y+=2500; point.z-=10000; }
    const auto p=sfFit(s,8);
    REQUIRE(p.score>4.25f);
    CHECK(1.f/p.c==doctest::Approx(100).epsilon(.001));
    CHECK(p.ny==doctest::Approx(1/std::sqrt(1.04f)).epsilon(.001));
}
TEST_CASE("orthographic fit uses affine distance rather than reciprocal distance") {
    SfSample s[9];
    for (int i=0;i<9;++i) {
        const float u=offsets[i][0],v=offsets[i][1],depth=20+v*.3f;
        s[i]={u,v,depth,u,0,depth,1};
    }
    const auto p=sfFit(s,8,1.f);
    CHECK(p.score>5.25f);
    CHECK(p.c==doctest::Approx(20));
    CHECK(p.b==doctest::Approx(.3));
}
TEST_CASE("motion trail bridges adjacent frames and remains bounded after a stall") {
    for (float fps : {20.f,30.f,60.f,120.f}) {
        const float advance=57.6f/fps, trail=trailLength(1/fps);
        CHECK(trail>=advance);
        CHECK(sfCoverage(-advance,0.18f,0.1f,trail)>0.01f);
        CHECK(sfCoverage(advance,0.18f,0.1f,trail)==0);
        CHECK(sfCoverage(-trail-1,0.18f,0.1f,trail)==0);
    }
    CHECK(trailLength(10)==doctest::Approx(3.6));
    CHECK(trailLength(0)==doctest::Approx(.6));
    for (float distance : {-5.f,-2.f,-.2f,0.f,.2f,2.f,5.f}) {
        CHECK(sfCoverage(distance,.18f,.3f,0)==doctest::Approx(sfCoverage(-distance,.18f,.3f,0)));
        CHECK(sfCoverage(distance,.18f,.3f,2.4f)>=0);
        CHECK(sfCoverage(distance,.18f,.3f,2.4f)<=1);
    }
}
TEST_CASE("distant subpixel bands fade together instead of forming a bright sheet") {
    CHECK(sfDensity(1,18)==1);
    CHECK(sfDensity(20,18)==0);
    float previous=1;
    for (int i=0;i<200;++i) {
        const float value=sfDensity(float(i)*.1f,18);
        CHECK(value<=previous); previous=value;
    }
    for (float span : {.1f,1.f,5.f,10.f}) {
        float minimum=100,maximum=0;
        for (int phase=0;phase<=100;++phase) {
            float energy=0;
            for (int pixel=-5;pixel<=5;++pixel)
                energy+=sfCoverage((pixel-phase*.01f)*span,.18f,span,0);
            minimum=std::min(minimum,energy); maximum=std::max(maximum,energy);
        }
        CHECK(maximum-minimum<0.15f);
    }
}
TEST_CASE("height phase leaves level ground unchanged and moves continuously along vertical faces") {
    for (float radius : {0.f,18.f,100.f,450.f}) {
        CHECK(sfPhase(radius,0,kContourRise)==doctest::Approx(radius));
        float previous=radius;
        for (int i=1;i<=200;++i) {
            const float h=i*.1f, phase=sfPhase(radius,h,kContourRise);
            CHECK(phase>previous);
            CHECK(phase-previous<=kContourRise*.1f+.0001f);
            CHECK(sfPhase(radius,-h,kContourRise)==doctest::Approx(phase));
            CHECK(sfPhase(radius,h,0)==doctest::Approx(radius));
            previous=phase;
        }
        CHECK(sfPhase(radius,.8f,kContourRise)-radius<.62f);
    }

    const float atJoin=sfPhase(40,3,kContourRise);
    CHECK(sfPhase(40.001f,3,kContourRise)-atJoin<.002f);
    CHECK(sfPhase(40,3.001f,kContourRise)-atJoin<.004f);
}
TEST_CASE("eight height-delayed bands avoid illuminating a whole tall wall together") {
    for (float radius : {10.f,60.f,150.f,300.f}) for (float fps : {20.f,30.f,60.f,120.f}) {
        int peakContour=0, peakRadial=0;
        float energyContour=0, energyRadial=0;
        for (int frame=0;frame<=int(10*fps);++frame) {
            int litContour=0, litRadial=0;
            float frameContour=0, frameRadial=0;
            const float front=pulseFront(frame/fps), pixelHeight=radius/800.f;
            for (int sample=0;sample<=120;++sample) {
                const float h=sample*.1f;
                const float phase=sfPhase(radius,h,kContourRise);
                const float footprint=std::max(.025f,sfPhase(radius,h+pixelHeight*.5f,kContourRise)-
                                                         sfPhase(radius,h-pixelHeight*.5f,kContourRise));
                float contour=0,radial=0;
                for (int band=0;band<8;++band) {
                    const float center=front-band*18.f;
                    if (center<=0 || center>450) continue;
                    contour=std::max(contour,sfCoverage(phase-center,.18f,footprint,trailLength(1/fps)));
                    radial=std::max(radial,sfCoverage(radius-center,.18f,.025f,trailLength(1/fps)));
                }
                litContour+=contour>.2f; litRadial+=radial>.2f;
                frameContour+=contour; frameRadial+=radial;
            }
            peakContour=std::max(peakContour,litContour); peakRadial=std::max(peakRadial,litRadial);
            energyContour=std::max(energyContour,frameContour); energyRadial=std::max(energyRadial,frameRadial);
        }
        CAPTURE(radius); CAPTURE(fps); CAPTURE(peakContour); CAPTURE(peakRadial);
        CHECK(peakRadial==121);
        CHECK(peakContour>0);
        CHECK(peakContour<55);
        CHECK(energyContour<energyRadial*.6f);
    }
}
TEST_CASE("dense grass counterexample demonstrates that plane support is not terrain identity") {
    SfSample s[9]; ground(s,10);
    for (int i : {0,3,5,7}) s[i]=groundSample(i,10,.8f);
    const auto p=sfFit(s,8);

    CHECK(p.score>5.25f);
    CHECK(1.f/p.c==doctest::Approx(6));
    CHECK(std::abs(1.f/p.c-10)>1);
}
TEST_CASE("stationary shader rings match the existing Survey lattice without raycast tables") {
    namespace old = zonai_survey::pure;
    for (unsigned i=0;i<old::kRings;++i) {
        const float radius=old::kRingTable.radius[i];
        CHECK(sfRingCoordinate(radius)==doctest::Approx(float(i+1)).epsilon(.00001));
        CHECK(sfRingRadius(float(i+1))==doctest::Approx(radius).epsilon(.00001));
        CHECK(sfStationaryRing(radius,.025f,.08f)>.99f);
        if (i>0) {
            const float between=(radius+old::kRingTable.radius[i-1])*.5f;
            CHECK(sfStationaryRing(between,.025f,.08f)==0);
        }
    }
    CHECK(sfStationaryRing(0,.025f,.08f)==0);
    CHECK(sfRingCoordinate(2.5f)==doctest::Approx(4));
    CHECK(sfRingCoordinate(25.f)==doctest::Approx(13));
    CHECK(old::kMaxRange<kScanRange);
    for (float radius : {2.5f,25.f}) {
        CHECK(sfRingCoordinate(radius+.001f)>sfRingCoordinate(radius-.001f));
        CHECK(sfRingCoordinate(radius+.001f)-sfRingCoordinate(radius-.001f)<.004f);
    }
}
TEST_CASE("imprint shares the old forward row layout cone and continuous eased clock") {
    namespace old=zonai_survey::pure;
    CHECK(old::kSurveyWidthDegrees==100);
    CHECK(scanConeCosHalf()==doctest::Approx(std::cos(50.f*old::kPi/180)));
    CHECK(std::cos(49.9f*old::kPi/180)>scanConeCosHalf());
    CHECK(std::cos(50.1f*old::kPi/180)<scanConeCosHalf());
    CHECK(kImprintSeconds==doctest::Approx(3.2));
    for (float heading : {0.f,.7f,2.1f,4.7f}) for (unsigned row=0;row<old::kRings;++row) {
        const float hx=std::sin(heading),hz=std::cos(heading);
        for (unsigned spoke : {0u,15u,32u,63u}) {
            float x,z; old::sampleOffset(row,spoke,hx,hz,x,z);
            CHECK(sfForward(x,z,hx,hz)==doctest::Approx(old::ringRadius(row)).epsilon(.0001));
            CHECK(sfStationaryRing(sfForward(x,z,hx,hz),.025f,.08f)>.99f);
        }
    }
    for (int i=0;i<=320;++i) {
        CHECK(imprintFront(i*.01f)==doctest::Approx(old::sweepFrontAt(i*.6f)));
        if (i) CHECK(imprintFront(i*.01f)>imprintFront((i-1)*.01f));
    }
    for (float d : {0.f,.625f,25.f,150.f,450.f})
        CHECK(imprintFront(imprintSecondsToReach(d))==doctest::Approx(sfRingCoordinate(d)).epsilon(.00001));
}
TEST_CASE("imprint starts fading locally after arrival and clears in the old sweep duration") {
    for (float forward : {1.f,25.f,100.f,300.f,450.f}) for (float h : {-200.f,-5.f,0.f,5.f,200.f}) {
        const float reveal=sfRingCoordinate(forward)+std::min(2.f,sfPhase(0,h,1)/12);
        CHECK(sfImprintEnvelope(forward,h,reveal-.01f)==0);
        CHECK(sfImprintEnvelope(forward,h,reveal+.35f)==doctest::Approx(1));
        CHECK(sfImprintEnvelope(forward,h,imprintFront(kImprintSeconds))==0);
        CHECK(sfImprintEnvelope(forward,h,-1)==1);
        float previous=0;
        for (int i=0;i<=35;++i) {
            const float value=sfImprintEnvelope(forward,h,reveal+i*.01f);
            CHECK(value>=previous-.000001f); previous=value;
        }
        previous=1;
        for (int i=1;i<=100;++i) {
            const float value=sfImprintEnvelope(forward,h,reveal+.35f+i*.1f);
            CHECK(value<=previous); previous=value;
        }
        CHECK(sfImprintEnvelope(forward,h,reveal+1)<.99f);
        CHECK(sfImprintEnvelope(forward,h,reveal+10)==0);
    }

    CHECK(sfImprintEnvelope(1,0,imprintFront(imprintSecondsToReach(300)))==0);
}
TEST_CASE("slope palette and world grid form thin lines instead of filled vertical faces") {
    const auto floor=sfImprint(0,0,25,0,1,0,.05f,.05f,.05f,25,.05f,-1,3,.08f,1);
    CHECK(floor.a>.99f); CHECK(floor.r==doctest::Approx(.10)); CHECK(floor.b==1);
    const auto wall=sfImprint(0,3.5f,25,0,0,1,.05f,.05f,.05f,25,.05f,-1,3,.08f,1);
    CHECK(wall.a>.8f); CHECK(wall.r==1); CHECK(wall.g==doctest::Approx(.20));
    int lit=0;
    for (int x=0;x<100;++x) for (int y=0;y<100;++y) {
        const auto p=sfImprint(x*.03f,3+y*.03f,25,0,0,1,.03f,.03f,.03f,25,.03f,-1,3,.08f,1);
        CHECK(std::isfinite(p.a)); CHECK(p.a>=0); CHECK(p.a<=1);
        lit+=p.a>.2f;
    }
    CHECK(lit>500); CHECK(lit<2500);
    CHECK(sfGridLine(-3,.025f,.01f,.002f)==doctest::Approx(sfGridLine(3,.025f,.01f,.002f)));
    for (float coordinate : {-7.4f,-3.05f,-.01f,0.f,.9f,2.99f,7.2f})
        CHECK(sfGridLine(coordinate,3,.1f,.08f)==doctest::Approx(sfGridLine(coordinate+3,3,.1f,.08f)).epsilon(.0001));
    for (float span : {3.f,6.f,20.f}) CHECK(sfGridLine(0,3,span,.08f)==0);
    for (int slope=0;slope<=100;++slope) {
        const float up=slope*.01f;
        const auto p=sfImprint(0,3,25,std::sqrt(1-up*up),up,0,.05f,.05f,.05f,25,.05f,-1,3,.08f,1);
        CHECK(p.a>=0); CHECK(p.a<=1);
        CHECK(p.r>=0); CHECK(p.r<=1); CHECK(p.g>=0); CHECK(p.g<=1); CHECK(p.b>=0); CHECK(p.b<=1);
    }
}
TEST_CASE("low vertical details get subdued blue rows without grid ribs or horizontal height slices") {
    for (float y : {-1.25f,-.5f,0.f,.8f,1.25f}) for (float x : {0.f,.8f,3.f,4.5f}) {
        const auto blade=sfImprint(x,y,25,0,0,1,.025f,.025f,.025f,25,.025f,-1,3,.08f,1);
        CHECK(blade.r==doctest::Approx(.1)); CHECK(blade.b==1);
        CHECK(blade.a==doctest::Approx(.3));
        const auto between=sfImprint(x,y,23.75f,0,0,1,.025f,.025f,.025f,23.75f,.025f,-1,3,.08f,1);
        CHECK(between.a==0);
    }

    const auto lowWall=sfImprint(0,.8f,25,0,0,1,.025f,.025f,.025f,25,.025f,-1,3,.08f,1);
    CHECK(lowWall.a<.31f); CHECK(lowWall.r<.11f);
}
TEST_CASE("vertical contours retain warm slope colour independently of rectangular grid confidence") {
    CHECK(sfGridConfidence(0)==0); CHECK(sfGridConfidence(6)==0); CHECK(sfGridConfidence(9)==1);
    for (float height : {-20.f,-4.f,4.f,20.f,80.f}) {
        for (float forward : {23.75f,25.f,27.f,30.f}) {
            const auto a=sfImprint(0,height,forward,0,0,1,.03f,.03f,.03f,forward,.03f,-1,3,.08f,0);
            const auto b=sfImprint(1.5f,height,forward,0,0,1,.03f,.03f,.03f,forward,.03f,-1,3,.08f,0);
            CHECK(a.r==doctest::Approx(1)); CHECK(a.g==doctest::Approx(.20)); CHECK(a.a<=.32f);
            CHECK(a.a==b.a);
            const auto supported=sfImprint(0,height,forward,0,0,1,.03f,.03f,.03f,forward,.03f,-1,3,.08f,1);
            CHECK(a.r==supported.r); CHECK(a.g==supported.g); CHECK(a.b==supported.b);
        }
    }
    float previous=0;
    for (int i=0;i<=90;++i) {
        const float confidence=sfGridConfidence(i*.1f);
        CHECK(confidence>=previous); CHECK(confidence<=1); previous=confidence;
    }
}
TEST_CASE("irregular contours join grid horizontals without a second phase or forward width") {
    for (float offset : {-.3f,-.12f,-.08f,-.03f,0.f,.03f,.08f,.12f,.3f}) {
        const float height=3+offset;
        const float horizontal=sfHeightContour(height,3,.025f,.08f,0);
        for (float forward : {23.75f,25.f,27.f,30.f})
            for (float forwardSpan : {.025f,.2f,1.f,4.f})
                for (int step=0;step<=10;++step) {
                    const float confidence=step*.1f;
                    const auto p=sfImprint(1.5f,height,forward,0,0,1,.025f,.025f,.025f,
                        forward,forwardSpan,-1,3,.08f,confidence);
                    CHECK(p.a==doctest::Approx(horizontal*(.32f+.68f*confidence)));
                    CHECK(p.a<=1);
                }
    }

    const auto junction=sfImprint(0,3,25,0,0,1,.025f,.025f,.025f,25,.025f,-1,3,.08f,1);
    CHECK(junction.a==doctest::Approx(1));
}
TEST_CASE("height contours keep their surface width on leaning faces") {
    for (float up : {0.f,.2f,.5f,.7f,.84f}) {
        const float gradient=std::sqrt(1-up*up);
        for (float distance : {-.3f,-.12f,-.08f,-.03f,0.f,.03f,.08f,.12f,.3f}) {
            const float flat=sfHeightContour(3+distance,3,.025f,.08f,0);
            const float leaning=sfHeightContour(3+distance*gradient,3,.025f*gradient,.08f,up);
            CHECK(leaning==doctest::Approx(flat).epsilon(.001));
        }
    }
    CHECK(std::isfinite(sfHeightContour(3,3,0,.08f,1)));
}
TEST_CASE("pruned fitting matches the exhaustive surface13 winner through grass and depth edges") {
    unsigned seed=314159;
    auto random=[&]() { seed=1664525u*seed+1013904223u; return float(seed>>8)/16777216.f; };
    for (int trial=0;trial<2400;++trial) {
        SfSample s[9];
        const float range=5+random()*440, slope=(random()-.5f)*.8f;
        ground(s,range);
        for (int i=0;i<9;++i) {
            if (trial%3==0) {
                const float depth=range+offsets[i][0]*slope;
                s[i]={offsets[i][0],offsets[i][1],1/depth,offsets[i][0]*depth/800,
                    2+offsets[i][1]*depth/800,depth,1};
            } else if (random()<.5f) s[i]=groundSample(i,range,random()*1.2f);
            s[i].y+=s[i].x*slope;
            if (trial%7==0 && random()<.2f) s[i].valid=0;
            if (trial%11==0) { s[i].x+=10000; s[i].y+=2500; s[i].z-=10000; }
        }
        for (int count : {1,4,8}) {
            const auto old=referenceFit(s,count,-1.f), fast=sfFit(s,count,-1.f);
            CHECK(fast.score==doctest::Approx(old.score).epsilon(.00001));
            CHECK(fast.a==old.a); CHECK(fast.b==old.b); CHECK(fast.c==old.c);
            CHECK(fast.nx==old.nx); CHECK(fast.ny==old.ny); CHECK(fast.nz==old.nz);
            CHECK(fast.offset==old.offset);
        }
    }
}
TEST_CASE("depth bounds enclose every candidate including extrapolation and invalid neighbors") {
    constexpr int triples[8][3]{{1,2,3},{2,3,4},{3,4,1},{4,1,2},{5,6,7},{6,7,8},{7,8,5},{8,5,6}};
    unsigned seed=271828;
    auto random=[&]() { seed=1664525u*seed+1013904223u; return float(seed>>8)/16777216.f; };
    for (int trial=0;trial<400;++trial) {
        SfSample s[9]{};
        for (int i=0;i<9;++i) {
            s[i].u=offsets[i][0]; s[i].v=offsets[i][1];
            s[i].q=trial%2 ? .00004f+random()*.2f : .1f+random()*25000;
            s[i].valid=i==0 || random()>.2f ? 1.f : 0.f;
        }
        const auto bounds=sfFitQBounds(s,6);
        for (const auto& t:triples) {
            const auto p=s[t[0]],q=s[t[1]],r=s[t[2]];
            if (!p.valid || !q.valid || !r.valid) continue;
            const float det=(q.u-p.u)*(r.v-p.v)-(r.u-p.u)*(q.v-p.v);
            const float a=((q.q-p.q)*(r.v-p.v)-(r.q-p.q)*(q.v-p.v))/det;
            const float b=((q.u-p.u)*(r.q-p.q)-(r.u-p.u)*(q.q-p.q))/det;
            const float c=p.q-a*p.u-b*p.v;
            for (float x : {-.5f,0.f,.5f}) for (float y : {-.5f,0.f,.5f}) {
                const float depth=std::max(.000001f,c+a*x+b*y);
                CHECK(depth>=bounds.lo); CHECK(depth<=bounds.hi);
            }
        }
    }
}
TEST_CASE("early temporal rejection never removes a live imprint including its height delay") {
    for (float forward : {-10.f,0.f,.625f,2.5f,25.f,80.f,250.f,450.f})
        for (float height : {-100.f,-2.f,0.f,1.25f,2.25f,30.f,100.f})
            for (int tick=0;tick<520;++tick) {
                const float front=tick*.1f;
                if (sfImprintEnvelope(forward,height,front)>0)
                    CHECK(sfMayImprintRange(forward-.05f,forward+.05f,front));
            }
    CHECK(sfMayImprintRange(20,30,-1));
    CHECK_FALSE(sfMayImprintRange(100,110,1));
    CHECK_FALSE(sfMayImprintRange(5,6,50));
}
TEST_CASE("sparse grass neighborhoods skip fitting for a substantial part of the scan lifetime") {
    int culled=0,total=0;
    for (int distance=0;distance<32;++distance) {
        const float range=5+distance*3.f;
        SfSample s[9]; ground(s,range);
        for (int i : {0,2,7}) s[i]=groundSample(i,range,.8f);
        const auto q=sfFitQBounds(s,6);
        const float lo=std::min(s[0].z,1/q.hi),hi=std::max(s[0].z,1/q.lo);
        for (int frame=0;frame<192;++frame) {
            ++total;
            culled+=!sfMayImprintRange(lo,hi,imprintFront(frame/60.f));
        }
    }
    CHECK(culled>total/3);
    std::printf("Synthetic grass work gate: %d/%d neighborhoods skip fitting (not GPU timing)\n",culled,total);
}
