#include <doctest.h>
#include <array>
#include <limits>
#include "RecallCameraFrame.hpp"

using namespace self_recall::pure;

TEST_CASE("Camera follows fractional and integer Recall heights without changing the orbit") {
    for (const float rate : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 4.0f}) {
        for (const float verticalSpeed : {-180.0f, 0.0f, 180.0f}) {
            for (const float eyeOffset : {-8.0f, 0.0f, 8.0f}) {
                std::array<float, 10> view{20, 101 + eyeOffset, 30, 20, 101, 22, 0, 1, 0, 0.8f};
                const auto before = view;
                CameraVerticalCorrection correction;
                const float shown = 100 + verticalSpeed * rate / 30;
                REQUIRE(alignCameraHeight(view.data(), shown, 100, 101, correction));
                CHECK(view[4] == doctest::Approx(shown + 1));
                CHECK(view[1] - view[4] == doctest::Approx(eyeOffset));
                for (unsigned i = 0; i < view.size(); ++i)
                    if (i != 1 && i != 4) CHECK(view[i] == before[i]);
            }
        }
    }
}

TEST_CASE("Camera removes a vertical transition offset independently of a cached root delay") {
    std::array<float, 10> view{10, 93, 30, 10, 91, 22, 0, 1, 0, 0.8f};
    CameraVerticalCorrection correction;
    REQUIRE(alignCameraHeight(view.data(), 105, 100, 101, correction));
    CHECK(correction.cachedRootDelta == 5);
    CHECK(correction.transitionDelta == 10);
    CHECK(correction.appliedDelta == 15);
    CHECK(view[1] == 108);
    CHECK(view[4] == 106);
    const auto once = view;
    REQUIRE(alignCameraHeight(view.data(), 105, 100, 101, correction));
    CHECK(view == once);
    CHECK(correction.appliedDelta == 0);
}

TEST_CASE("Camera rejects invalid or overflowing coordinates without partially changing the packet") {
    const std::array<float, 10> original{10, 103, 30, 10, 101, 22, 0, 1, 0, 0.8f};
    CameraVerticalCorrection correction;
    CHECK_FALSE(alignCameraHeight(nullptr, 100, 100, 101, correction));
    for (const float invalid : {std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN()}) {
        auto view = original;
        CHECK_FALSE(alignCameraHeight(view.data(), invalid, 100, 101, correction));
        CHECK(view == original);
        CHECK_FALSE(alignCameraHeight(view.data(), 100, invalid, 101, correction));
        CHECK(view == original);
        CHECK_FALSE(alignCameraHeight(view.data(), 100, 100, invalid, correction));
        CHECK(view == original);
    }
    auto view = original;
    CHECK_FALSE(alignCameraHeight(view.data(), std::numeric_limits<float>::max(),
                                 -std::numeric_limits<float>::max(), 101, correction));
    CHECK(view == original);
    view[9] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(alignCameraHeight(view.data(), 105, 100, 101, correction));
    CHECK(view[1] == original[1]);
    CHECK(view[4] == original[4]);
}
