// SPDX-License-Identifier: GPL-2.0-only
//
// Shared TotK runtime support - pure status-text memory for the solo harness.
// Copyright (C) HanSoloBlaster, afiaka87
//
// Exact extraction of the donor SoloHarness helpers (matrix H2). The 95-character
// compare window is a deliberate contract: two statuses that agree on their first
// 95 characters are treated as the same status (no banner retrigger), and a
// remembered status is truncated to 95 characters plus the terminator.

#pragma once

#include <cstddef>

namespace totk::harness {

inline constexpr std::size_t kStatusCap = 96;

// Donor sameText(): null-tolerant; compares at most the first 95 characters.
inline bool statusEquals(const char* a, const char* b) {
    if (!a || !b) return a == b;
    for (int i = 0; i < 95; ++i) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return true;
}

// Donor rememberStatus(): copy up to 95 characters into dst (capacity kStatusCap),
// always NUL-terminated; a null source clears dst.
inline void copyStatus(char* dst, const char* text) {
    int i = 0;
    if (text) {
        for (; i < 95 && text[i]; ++i) dst[i] = text[i];
    }
    dst[i] = 0;
}

} // namespace totk::harness
