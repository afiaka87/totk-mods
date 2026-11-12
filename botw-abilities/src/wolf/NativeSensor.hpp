// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "totk/core/Units.hpp"
#include <cstdint>
#include <optional>

namespace botw_abilities::wolf {

bool initializeSensorApi(std::uintptr_t base);

// Where the Purah Pad sensor is pointing, only while it is reacting to something.
[[nodiscard]] std::optional<totk::core::WorldPosition> readSensorTarget();
[[nodiscard]] bool playerRiding();

}
