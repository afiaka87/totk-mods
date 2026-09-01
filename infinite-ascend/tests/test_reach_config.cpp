#include <doctest.h>

#include <bit>
#include <cstdint>
#include <limits>

#include "ReachConfig.hpp"

using namespace zonai_ascend::pure;

TEST_CASE("release reach fixes every native span to ten kilometres") {
    const auto reach = deriveReach(kReleaseReach);
    CHECK(reach.total == doctest::Approx(10000.0f));
    CHECK(reach.validationSpan ==
          doctest::Approx(9999.5f));
    CHECK(reach.markerSpan == doctest::Approx(9998.2f));
    CHECK(reach.validationSpan +
              kValidationStartAbovePlayer ==
          doctest::Approx(reach.total));
    CHECK(reach.markerSpan + kMarkerStartAbovePlayer ==
          doctest::Approx(reach.total));
}

TEST_CASE("invalid reach derivation fails closed to native") {
    CHECK(deriveReach(0.0f).total ==
          doctest::Approx(kNativeReach));
    CHECK(deriveReach(10000.5f).total ==
          doctest::Approx(kNativeReach));
    const float nan =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(deriveReach(nan).total ==
          doctest::Approx(kNativeReach));
}

TEST_CASE("fixed lenient policy admits only proven local shape reasons") {
    CHECK(canRelaxQueryFailure(kAngleReason));
    CHECK(canRelaxQueryFailure(kSurfaceDisagreementReason));
    CHECK(canRelaxQueryFailure(
        kAngleReason | kSurfaceDisagreementReason));
    CHECK(canRelaxQueryFailure(
        kQueryBaselineReason | kAngleReason));

    CHECK_FALSE(canRelaxQueryFailure(0));
    CHECK_FALSE(canRelaxQueryFailure(kQueryBaselineReason));
    CHECK_FALSE(canRelaxQueryFailure(kAngleReason | 0x20u));
    CHECK_FALSE(canRelaxQueryFailure(
        kSurfaceDisagreementReason | 0x80u));
    CHECK_FALSE(canRelaxQueryFailure(
        kAngleReason | 0x1000u));
    CHECK_FALSE(canRelaxQueryFailure(
        kSurfaceDisagreementReason | 0x4000u));
}

TEST_CASE("lenient clearing preserves baseline and unknown bits") {
    CHECK(clearLenientReasons(
              kQueryBaselineReason | kAngleReason |
              kSurfaceDisagreementReason) ==
          kQueryBaselineReason);
    CHECK(clearLenientReasons(
              kAngleReason | 0x80u) ==
          0x80u);
}

TEST_CASE("release marker curve is restrained and bounded") {
    CHECK(markerScale(0.0f) == doctest::Approx(1.0f));
    CHECK(markerScale(40.0f) == doctest::Approx(1.0f));
    CHECK(markerScale(160.0f) == doctest::Approx(1.5f));
    CHECK(markerScale(280.0f) == doctest::Approx(2.0f));
    CHECK(markerScale(520.0f) == doctest::Approx(3.0f));
    CHECK(markerScale(760.0f) == doctest::Approx(4.0f));
    CHECK(markerScale(3000.0f) == doctest::Approx(4.0f));
    CHECK(markerScale(10000.0f) == doctest::Approx(4.0f));
}

TEST_CASE("invalid marker distances restore native size") {
    const float nan =
        std::numeric_limits<float>::quiet_NaN();
    CHECK(markerScale(nan) == doctest::Approx(1.0f));
    CHECK(markerScale(-1.0f) == doctest::Approx(1.0f));
    CHECK(markerScale(10000.5f) ==
          doctest::Approx(1.0f));
}
