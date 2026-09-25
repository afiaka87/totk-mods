// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cstdint>

namespace zonai_survey::engine {

using RaycastFn = std::uint64_t (*)(const void*, const void*, const void*,
                                    const void*, std::uint32_t, std::uint32_t);

}
