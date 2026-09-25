// SPDX-License-Identifier: MIT
#include "FidelityPolicy.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <limits>
using namespace survey_fidelity;
TEST_CASE("eight equally spaced bands move outward on one bounded clock") {
    CHECK(pulseFront(-1) == 0);
    CHECK(pulseFront(0) == 0);
    CHECK(pulseFront(kScanSeconds+1) == pulseFront(kScanSeconds));
    CHECK(pulseFront(kScanSeconds) == kScanRange + 7*kBandSpacing);
    CHECK(kScanSeconds == 10.0f);
    CHECK(kBandOpacity == 1.0f);
    for (unsigned i=1;i<10;++i)
        CHECK(pulseFront(float(i))-pulseFront(float(i-1)) == doctest::Approx(57.6f));
    for (unsigned i = 1; i < 1000; ++i)
        CHECK(pulseFront(i*0.01f) > pulseFront((i-1)*0.01f));
    const float front = 180;
    for (unsigned i = 0; i < 8; ++i) {
        const float radius = front-i*kBandSpacing;
        CHECK(bandDistance(radius, front) == 0);
        CHECK(bandDistance(radius-9, front) == 9);
    }
    CHECK(bandDistance(0, 0) > 1000);
    CHECK(bandDistance(20, pulseFront(kScanSeconds)) > kBandHalfWidth);
    for (float radius : {1.f, 20.f, 100.f, 250.f, 450.f})
        CHECK(pulseFront(secondsToReach(radius)) == doctest::Approx(radius).epsilon(0.001));
}
TEST_CASE("sky and invalid depth never reconstruct a surface") {
    CHECK(validDepth(0)); CHECK(validDepth(0.999f));
    CHECK_FALSE(validDepth(1)); CHECK_FALSE(validDepth(-0.001f));
    CHECK_FALSE(validDepth(std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(validDepth(std::numeric_limits<float>::infinity()));
    CHECK(edgeWidth(20, 500, 800) == doctest::Approx(0.025));
    CHECK(edgeWidth(20, 20.1f, 800) == doctest::Approx(0.20));
    CHECK(edgeWidth(20, 20.1f, 0) == doctest::Approx(0.025));
}
TEST_CASE("native normalized-linear depth round trips perspective and orthographic surfaces") {
    for (bool ortho : {false, true}) for (float n : {0.1f, 1.f}) for (float f : {500.f, 10000.f}) {
        float projection[16]{};
        projection[0] = 1.2f; projection[5] = 1.7f;
        if (ortho) {
            projection[10] = -2/(f-n); projection[11] = -(f+n)/(f-n); projection[15] = 1;
        } else {
            projection[10] = -(f+n)/(f-n); projection[11] = -2*f*n/(f-n); projection[14] = -1;
        }
        float inverse[16]; REQUIRE(inverse4(projection, inverse));
        float nativeNear{}, nativeFar{};
        REQUIRE(depthRange(inverse, nativeNear, nativeFar));
        for (float distance : {2.f,20.f,100.f,450.f}) for (float u : {0.05f,0.5f,0.95f}) for (float v : {0.05f,0.5f,0.95f}) {
            Point p{};
            const float d = (distance-nativeNear)/(nativeFar-nativeNear);
            REQUIRE(reconstruct(inverse, u, v, d, p));
            CHECK(-p.z == doctest::Approx(distance).epsilon(0.001));
            const float w = ortho ? 1.f : -p.z;
            CHECK((projection[0]*p.x/w+1)*0.5f == doctest::Approx(u).epsilon(0.001));
            CHECK((projection[5]*p.y/w+1)*0.5f == doctest::Approx(v).epsilon(0.001));
        }
        Point sky{}; CHECK_FALSE(reconstruct(inverse, 0.5f, 0.5f, 1, sky));
    }
}
TEST_CASE("normal is the default and diagnostic cycling returns to normal") {
    Mode mode{};
    CHECK(mode == Mode::Off);
    mode = nextMode(mode); CHECK(mode == Mode::Calibration);
    mode = nextMode(mode); CHECK(mode == Mode::UniformCheck);
    mode = nextMode(mode); CHECK(mode == Mode::RawDepth);
    mode = nextMode(mode); CHECK(mode == Mode::Depth);
    mode = nextMode(mode); CHECK(mode == Mode::Stripe);
    CHECK(nextMode(mode) == Mode::Off);
}
TEST_CASE("stored clip distances remove far-endpoint quantization as camera near changes") {
    float legacyMin = 1e10f, legacyMax = -1e10f;
    for (unsigned i = 10; i <= 100; ++i) {
        const float n = float(i)*0.01f, f = 25000.f;
        const float projection[16]{1.2f,0,0,0, 0,1.7f,0,0,
            0,0,-(f+n)/(f-n),-2*f*n/(f-n), 0,0,-1,0};
        float inverse[16]; REQUIRE(inverse4(projection, inverse));
        const float d = (144.f-n)/(f-n);
        Point old{}, stable{};
        REQUIRE(reconstruct(inverse, 0.7f, 0.4f, d, old));
        REQUIRE(reconstructLinear(inverse, 0.7f, 0.4f, d, n, f, true, stable));
        legacyMin = std::min(legacyMin, -old.z); legacyMax = std::max(legacyMax, -old.z);
        CHECK(-stable.z == doctest::Approx(144.f).epsilon(0.00001));
        CHECK(stable.x == doctest::Approx(48.f).epsilon(0.00001));
    }
    CHECK(legacyMax-legacyMin > 0.1f);
}
TEST_CASE("clip-aware reconstruction matches independent perspective and orthographic fixtures") {
    for (bool perspective : {false,true}) for (float n : {0.1f,1.f}) {
        const float f = 25000;
        float projection[16]{1.2f,0,0,0, 0,1.7f,0,0, 0,0,0,0, 0,0,0,0};
        projection[10] = perspective ? -(f+n)/(f-n) : -2/(f-n);
        projection[11] = perspective ? -2*f*n/(f-n) : -(f+n)/(f-n);
        projection[14] = perspective ? -1.f : 0.f; projection[15] = perspective ? 0.f : 1.f;
        float inverse[16]; REQUIRE(inverse4(projection, inverse));
        for (float distance : {2.f,20.f,144.f,450.f}) {
            Point p{}; const float d = (distance-n)/(f-n);
            REQUIRE(reconstructLinear(inverse, 0.7f, 0.4f, d, n, f, perspective, p));
            CHECK(-p.z == doctest::Approx(distance).epsilon(0.00001));
            CHECK(p.x == doctest::Approx((perspective ? distance : 1.f)/3).epsilon(0.00001));
            CHECK_FALSE(reconstructLinear(inverse, 0.7f, 0.4f, 1.f, n, f, perspective, p));
        }
    }
    CHECK_FALSE(validClipRange(0, 25000)); CHECK_FALSE(validClipRange(2,1));
}
TEST_CASE("pixel coverage preserves a thin line across subpixel motion without an on-off jump") {
    for (unsigned frame = 0; frame <= 100; ++frame) {
        const float centre = float(frame)*0.01f;
        float energy = 0;
        for (int pixel = -4; pixel <= 4; ++pixel) energy += pixelLineCoverage(float(pixel)-centre, 0.18f, 1.f);
        CHECK(energy == doctest::Approx(0.7f).epsilon(0.00001));
    }
    CHECK(pixelLineCoverage(10, 0.18f, 1000) == 0);
    CHECK(pixelLineCoverage(0, 0.18f, 0) == 1);
    for (float d : {0.f,0.2f,0.5f,1.f})
        CHECK(pixelLineCoverage(d, 0.18f, 1) == doctest::Approx(pixelLineCoverage(-d,0.18f,1)));
}
TEST_CASE("diagnostic stages isolate uniforms from texture sampling") {
    CHECK_FALSE(usesDepth(Mode::Off)); CHECK_FALSE(usesUniforms(Mode::Off));
    CHECK_FALSE(usesDepth(Mode::Calibration)); CHECK_FALSE(usesUniforms(Mode::Calibration));
    CHECK_FALSE(usesDepth(Mode::UniformCheck)); CHECK(usesUniforms(Mode::UniformCheck));
    CHECK(usesDepth(Mode::RawDepth)); CHECK_FALSE(usesUniforms(Mode::RawDepth));
    CHECK(usesDepth(Mode::Depth)); CHECK(usesUniforms(Mode::Depth));
    CHECK(usesDepth(Mode::Stripe)); CHECK(usesUniforms(Mode::Stripe));
    CHECK(programIndex(Mode::Calibration) == 0); CHECK(programIndex(Mode::UniformCheck) == 1);
    CHECK(programIndex(Mode::RawDepth) == 2); CHECK(programIndex(Mode::Depth) == 3);
    CHECK(programIndex(Mode::Stripe) == 3);
}
TEST_CASE("depth selection requires the correct availability bits") {
    CHECK(depthSlot(0) == 0); CHECK(depthSlot(4) == 0);
    CHECK(depthSlot(1) == 0x700); CHECK(depthSlot(2) == 0xe40);
    CHECK(depthSlot(3) == 0x700); CHECK(depthSlot(0x1000) == 0);
    CHECK_FALSE(validDimensions(0, 720)); CHECK_FALSE(validDimensions(1280, 0));
    CHECK_FALSE(validDimensions(65535, 720)); CHECK_FALSE(validDimensions(1280, 8193));
    CHECK(validDimensions(1280, 720)); CHECK(validDimensions(3840, 2160));
}
TEST_CASE("trace writes stay bounded without integer wrap") {
    CHECK(traceFits(0, 256)); CHECK(traceFits(kTraceCapacity - 1, 1));
    CHECK_FALSE(traceFits(kTraceCapacity, 1)); CHECK_FALSE(traceFits(0, 0));
    CHECK_FALSE(traceFits(kTraceCapacity + 1, 1));
    CHECK_FALSE(traceFits(1, std::numeric_limits<std::uint64_t>::max()));
}
TEST_CASE("inverse rejects singular and nonfinite data") {
    float matrix[16]{}, output[16]{};
    CHECK_FALSE(inverse4(matrix, output));
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
    matrix[1] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(inverse4(matrix, output));
    matrix[1] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(inverse4(matrix, output));
}
TEST_CASE("inverse preserves row major projection and translated rotated camera") {
    const float cases[][16]{
        {2,0,0,0, 0,3,0,0, 0,0,-1.02f,-2.02f, 0,0,-1,0},
        {0,0,1,-30, 0,1,0,5, -1,0,0,20, 0,0,0,1},
        {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}
    };
    for (const auto& matrix : cases) {
        float inverse[16]; REQUIRE(inverse4(matrix, inverse));
        for (unsigned r = 0; r < 4; ++r) for (unsigned c = 0; c < 4; ++c) {
            float sum = 0;
            for (unsigned k = 0; k < 4; ++k) sum += matrix[r*4+k] * inverse[k*4+c];
            CHECK(sum == doctest::Approx(r == c ? 1.f : 0.f).epsilon(0.00001));
        }
    }
}
