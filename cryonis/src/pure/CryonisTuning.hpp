// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
namespace cryonis {
// build_pillar.py reads these literals; scale remains a percentage of original dimensions.
inline constexpr float kOriginalWidth = 3.0f;
inline constexpr float kOriginalHeight = 4.0f;
inline constexpr float kSizePercent = 90.0f;
inline constexpr float kSettledSubmergePercent = 5.0f;
inline constexpr float kSizeScale = kSizePercent / 100.0f;
inline constexpr float kWidth = kOriginalWidth * kSizeScale;
inline constexpr float kHeight = kOriginalHeight * kSizeScale;
inline constexpr float kSettledSubmerge = kHeight * kSettledSubmergePercent / 100.0f;
}
