// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <limits>
#include "FlightDiagnostics.hpp"

using namespace arrowbound::pure;
TEST_CASE("flight trace sampling is bounded and includes launch") {
    CHECK_FALSE(traceSampleDue(0));
    for (unsigned n = 1; n <= 8; ++n) CHECK(traceSampleDue(n));
    CHECK_FALSE(traceSampleDue(9));
    CHECK(traceSampleDue(12));
    CHECK(traceSampleDue(1800));
    CHECK_FALSE(traceSampleDue(1806));
}
TEST_CASE("flight trace quantization never converts invalid floats to integers") {
    CHECK(traceNumber(1.25f) == 125);
    CHECK(traceNumber(-1.25f) == -125);
    CHECK(traceNumber(1.f, 1000) == 1000);
    CHECK(traceNumber(std::numeric_limits<float>::infinity()) == std::numeric_limits<int>::min());
    CHECK(traceNumber(std::numeric_limits<float>::quiet_NaN()) == std::numeric_limits<int>::min());
    CHECK(traceNumber(std::numeric_limits<float>::max()) == 2147483647);
    CHECK(traceNumber(-std::numeric_limits<float>::max()) == -2147483647);
}
