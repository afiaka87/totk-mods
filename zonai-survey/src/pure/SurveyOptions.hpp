// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "PulseLattice.hpp"

#ifndef SURVEY_CONSTRAINED
#define SURVEY_CONSTRAINED 0
#endif

namespace zonai_survey::pure {
inline constexpr float kRangeOptions[]{60.f, 90.f, 120.f, 180.f, 240.f, kMaxRange};
inline constexpr unsigned kCooldownOptions[]{0, 1, 2, 3, 5, 7, 10};
inline constexpr float kConstrainedRange = 180.f;
inline constexpr unsigned kRegularCooldown = 3, kConstrainedCooldown = 7;
inline constexpr std::uint64_t kSystemTicksPerSecond = 19200000;
inline constexpr const char* kSurveyRefusalCue = "mc_AmiiboError";

struct SurveyOptions {
    unsigned rangeIndex{3}, cooldownIndex{5};
    float range() const { return kRangeOptions[rangeIndex]; }
    unsigned cooldownSeconds() const { return kCooldownOptions[cooldownIndex]; }
    void cycleRange() { rangeIndex = (rangeIndex + 1) % 6; }
    void cycleCooldown() { cooldownIndex = (cooldownIndex + 1) % 7; }
};

struct SurveyCooldown {
    std::uint64_t started{}, duration{};
    void start(std::uint64_t now, unsigned seconds) {
        started = now; duration = std::uint64_t(seconds) * kSystemTicksPerSecond;
    }
    std::uint64_t remaining(std::uint64_t now) const {
        if (now < started || now - started >= duration) return 0;
        return duration - (now - started);
    }
};

inline bool withinSurveyRange(float dx, float dz, float range) {
    return dx*dx + dz*dz <= range*range;
}
}

namespace zonai_survey::options {
#if SURVEY_CONSTRAINED
inline constexpr float nextRange() { return pure::kConstrainedRange; }
inline constexpr unsigned cooldownSeconds() { return pure::kConstrainedCooldown; }
#else
inline constexpr float nextRange() { return pure::kMaxRange; }
inline constexpr unsigned cooldownSeconds() { return pure::kRegularCooldown; }
#endif
}
