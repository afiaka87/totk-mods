// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

namespace descend::pure {

constexpr float upwardMarkerScale(float height) {
    if (!(height > 300.0f && height < 100000.0f)) return 1.0f;
    return height >= 1500.0f ? 3.0f : 1.0f + (height - 300.0f) / 600.0f;
}

} // namespace descend::pure
