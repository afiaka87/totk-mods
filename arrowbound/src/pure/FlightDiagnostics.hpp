// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace arrowbound::pure {
inline int traceNumber(float value, float scale = 100.0f) {
    const double scaled = static_cast<double>(value) * scale;
    if (!std::isfinite(scaled)) return std::numeric_limits<int>::min();
    if (scaled > 2147483647.0) return 2147483647;
    if (scaled < -2147483647.0) return -2147483647;
    return static_cast<int>(scaled);
}
inline bool traceSampleDue(std::uint32_t sample) {
    return sample != 0 && sample <= 1800 && (sample <= 8 || sample % 6 == 0);
}
}
