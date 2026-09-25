// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#include "doctest.h"

#include "PerfReport.hpp"

using namespace zonai_survey;

TEST_CASE("perf report converts counter ticks to wall time") {

    CHECK(pure::perfMicros(19200000ull, 19200000ull) == 1000000u);
    CHECK(pure::perfMillis(19200000ull, 19200000ull) == 1000u);

    CHECK(pure::perfMicros(19200ull, 19200000ull) == 1000u);

    CHECK(pure::perfMicros(19ull, 19200000ull) == 0u);
    CHECK(pure::perfMicros(20ull, 19200000ull) == 1u);
    CHECK(pure::perfMicros(0ull, 19200000ull) == 0u);
}

TEST_CASE("perf report saturates instead of wrapping") {

    CHECK(pure::perfMicros(12345ull, 0ull) == 0u);

    CHECK(pure::perfMicros(1000000000000ull, 19200000ull) == 0xFFFFFFFFu);

    CHECK(pure::perfMicros(0x0FFFFFFFFFFFFFFFull, 19200000ull) == 0xFFFFFFFFu);
    CHECK(pure::perfMicros(0xFFFFFFFFFFFFFFFFull, 19200000ull) == 0xFFFFFFFFu);
}
