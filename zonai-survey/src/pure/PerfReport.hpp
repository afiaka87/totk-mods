// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cstdint>

namespace zonai_survey::pure {

constexpr std::uint32_t perfMicros(std::uint64_t deltaTicks,
                                   std::uint64_t frequency) {
    if (frequency == 0) return 0;

    if (deltaTicks > 0xFFFFFFFFFFFFFFFFull / 1000000ull) return 0xFFFFFFFFu;
    const std::uint64_t micros = deltaTicks * 1000000ull / frequency;
    return micros > 0xFFFFFFFFull ? 0xFFFFFFFFu
                                  : static_cast<std::uint32_t>(micros);
}

constexpr std::uint32_t perfMillis(std::uint64_t deltaTicks,
                                   std::uint64_t frequency) {
    return perfMicros(deltaTicks, frequency) / 1000u;
}

}
