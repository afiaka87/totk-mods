// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "Totk121Layout.hpp"
#include <cstdint>
namespace cryonis::engine {
template <class Reader> bool amiiboSelected(std::uintptr_t screen, Reader read) {
    const auto count = read.template operator()<std::int32_t>(screen + layout::wheel::kCount);
    const auto cursor = read.template operator()<std::int32_t>(screen + layout::wheel::kCursor);
    if (count < 1 || count > 8 || cursor < 0 || cursor >= count)
        return false;
    if (read.template operator()<std::uint8_t>(screen + layout::wheel::kBusy) ||
        !read.template operator()<std::uint8_t>(screen + layout::wheel::kHighlight))
        return false;
    return read.template operator()<std::uint32_t>(screen + layout::wheel::kIds + 4 * cursor) ==
           layout::wheel::kAmiibo;
}
}
