#pragma once

#include <cmath>
#include <cstdint>

namespace linked_stick::pure {

struct TelemetryVector {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

[[nodiscard]] inline bool isFinite(TelemetryVector value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

}
