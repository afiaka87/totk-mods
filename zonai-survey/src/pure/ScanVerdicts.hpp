// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cstdint>
#include "SurveyOptions.hpp"

namespace zonai_survey::pure {

enum class ScanVerdict : uint8_t {
    Accepted,
    AlreadyPulsing,
    PlayerUnresolved,
    CoolingDown,
};

enum class ScanAbandonReason : uint8_t {
    PlayerLost,
    WorldReloaded,
    Expired,
};

inline bool isOrdinaryEnding(ScanAbandonReason reason) {
    return reason == ScanAbandonReason::Expired;
}

}
