// SPDX-License-Identifier: MIT
#include "PulsePolicy.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <limits>
using namespace zonai_survey::pulse;
TEST_CASE("depth selection requires the correct availability bits") {
    CHECK(depthSlot(0) == 0); CHECK(depthSlot(4) == 0);
    CHECK(depthSlot(1) == 0x700); CHECK(depthSlot(2) == 0xe40);
    CHECK(depthSlot(3) == 0x700); CHECK(depthSlot(0x1000) == 0);
    CHECK_FALSE(validDimensions(0, 720)); CHECK_FALSE(validDimensions(1280, 0));
    CHECK_FALSE(validDimensions(65535, 720)); CHECK_FALSE(validDimensions(1280, 8193));
    CHECK(validDimensions(1280, 720)); CHECK(validDimensions(3840, 2160));
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
