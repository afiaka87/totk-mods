// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace botw_abilities::wolf {
inline constexpr std::uint64_t kRevivalTicks = 7 * 60 * 30;

constexpr std::uint64_t revivalRemaining(std::uint64_t now, std::uint64_t diedAt, bool died) {
    const auto elapsed = now - diedAt;
    return died && elapsed < kRevivalTicks ? kRevivalTicks - elapsed : 0;
}
}
