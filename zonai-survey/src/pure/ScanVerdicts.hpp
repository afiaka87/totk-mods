// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cstdint>
#include "SurveyOptions.hpp"

namespace zonai_survey::pure {

enum class ScanVerdict : uint8_t {
    Accepted,
    PlayerUnresolved,
    CoolingDown,
};

}
