// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>

#include "doctest.h"

#include "../src/support/TextFormat.hpp"

namespace {

std::string ours(const char* format, ...) {
    char text[256];
    std::va_list args;
    va_start(args, format);
    botw_abilities::support::formatText(text, sizeof(text), format, args);
    va_end(args);
    return text;
}

std::string reference(const char* format, ...) {
    char text[256];
    std::va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return text;
}

int measured(char* out, std::size_t size, const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    const int length = botw_abilities::support::formatText(out, size, format, args);
    va_end(args);
    return length;
}

#define SAME(...) CHECK(ours(__VA_ARGS__) == reference(__VA_ARGS__))

}  // namespace

TEST_CASE("integers match the standard formatter") {
    SAME("%d %i %u", -5, 12, 7u);
    SAME("[%5d] [%-5d] [%05d] [%+d] [% d]", 42, 42, 42, 42, 42);
    SAME("[%.3d] [%5.3d] [%.0d]", 7, 7, 0);
    SAME("%x %X %o %#x %#X %#o", 255u, 255u, 8u, 255u, 255u, 8u);
    SAME("%08x %8x %-8x|", 0xBEEFu, 0xBEEFu, 0xBEEFu);
    SAME("%#x %#o", 0u, 0u);
    SAME("%hhd %hhu %hd %hu", 200, 200, 70000, 70000);
    SAME("%ld %lu %lld %llu %zu %lx %llx", -9L, 9UL, -9LL, 18446744073709551615ULL, std::size_t(9), 0xFFUL, 0xFFFFFFFFFFULL);
    SAME("%lld", (std::numeric_limits<long long>::min)());
    SAME("%d", (std::numeric_limits<int>::min)());
    SAME("%*d %-*d|", 4, 1, 4, 2);
    SAME("%*d|", -4, 1);
}

TEST_CASE("text and literals match the standard formatter") {
    SAME("[%s] [%5s] [%-5s] [%.2s] [%.*s]", "abc", "abc", "abc", "abc", 1, "abc");
    SAME("[%c] [%3c] [%-3c]", 'x', 'y', 'z');
    SAME("100%% done");
    SAME("[wolf-link] MOVE id=%u far=%.1f gear=%d", 3u, 4.25f, 2);
}

TEST_CASE("fixed decimals match the standard formatter") {
    SAME("%f %f %f", 0.0, 1.5, -1.5);
    SAME("%.1f %.2f %.3f %.0f", 3.14159, 3.14159, 3.14159, 3.14159);
    SAME("%.0f %.0f %.0f %.0f", 0.5, 1.5, 2.5, 3.5);
    SAME("%.1f %.1f %.1f", 0.25, 0.75, 1.25);
    SAME("%.2f %.2f", 0.999, 9.999);
    SAME("%8.2f|%-8.2f|%08.2f|%+.2f|% .2f", 1.5, 1.5, -1.5, 1.5, 1.5);
    SAME("%f %f", 123456.789f, -0.0f);
    SAME("%.3f", 1234567890123.0);
    SAME("%#.0f", 3.0);
    SAME("%f", 1e-7);
}

TEST_CASE("exponent and general forms match the standard formatter") {
    SAME("%e %E %.2e", 1234.5, 1234.5, 0.00012);
    SAME("%e %.0e", 0.0, 9.6);
    SAME("%e", 1e100);
    SAME("%.2e", 9.999);
    SAME("%g %g %g %g", 0.0001, 0.00001, 100000.0, 1000000.0);
    SAME("%.5g %.5g %.5g", 3.14159265, 123456.0, 0.5);
    SAME("%G %G", 1e-10, 2.5);
    SAME("%#g", 1.5);
    SAME("%.0g %.1g", 2.5, 0.25);
}

TEST_CASE("non-finite values") {
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(ours("%f %f", inf, -inf) == "inf -inf");
    CHECK(ours("%F", inf) == "INF");
    CHECK(ours("%f", nan) == "nan");
    CHECK(ours("%05f|%5.1f", inf, inf) == "  inf|  inf");
}

TEST_CASE("the output is bounded and reports the full length") {
    char text[6] = "?????";
    CHECK(measured(text, sizeof(text), "%s-%d", "abcdef", 12) == 9);
    CHECK(std::string(text) == "abcde");
    CHECK(measured(nullptr, 0, "%d", 12345) == 5);
    char one[1] = {'?'};
    CHECK(measured(one, 1, "abc") == 3);
    CHECK(one[0] == '\0');
}

TEST_CASE("pointers, missing text and unknown conversions") {
    CHECK(ours("%p", reinterpret_cast<void*>(std::uintptr_t(0xABC0))) == "0xabc0");
    CHECK(ours("%p", static_cast<void*>(nullptr)) == "0x0");
    CHECK(ours("[%s]", static_cast<const char*>(nullptr)) == "[(null)]");
    CHECK(ours("%q!") == "%q!");
    CHECK(ours("end%") == "end%");
    CHECK(ours("end%5") == "end%5");
}
