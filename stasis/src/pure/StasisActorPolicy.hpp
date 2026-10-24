// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <string_view>

namespace stasis {
inline bool livingTarget(std::string_view name) {
    return name.starts_with("Enemy_") || name == "Animal_RupeeRabbit_A" ||
        name == "Animal_RupeeRabbit_FromCaveMaster" || name == "Animal_RupeeRabbit_NushiShop";
}
} // namespace stasis
