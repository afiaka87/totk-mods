// SPDX-License-Identifier: MIT
#pragma once

#include "ScanVerdicts.hpp"

namespace zonai_survey::presentation {

inline const char* displayText(pure::ScanVerdict verdict) {
    switch (verdict) {
        case pure::ScanVerdict::Accepted: return "surveying";
        case pure::ScanVerdict::PlayerUnresolved:
            return "cannot find Link right now";
        case pure::ScanVerdict::CoolingDown: return "survey is recharging";
    }
    return "not ready";
}

}
