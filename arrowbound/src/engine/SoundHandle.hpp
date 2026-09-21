// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <cstdint>
#include <cstddef>

namespace arrowbound::engine {

struct SoundHandle {
    std::int8_t system = 0;
    std::uint8_t reserved = 0;
    std::int16_t index = -1;
    std::uint32_t createId = 0;
};
static_assert(sizeof(SoundHandle) == 8);
static_assert(offsetof(SoundHandle, index) == 2 && offsetof(SoundHandle, createId) == 4);

} // namespace arrowbound::engine
