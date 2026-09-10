#pragma once
#include <cmath>
#include <cstdint>

namespace self_recall::pure {
inline constexpr float kRecallStaminaPerSecond = 28.0f;
inline constexpr std::uint64_t kRecallReleaseProtectionNs = 200000000;
enum class StaminaStatus { Unavailable, Empty, Available };
inline StaminaStatus staminaStatus(float normal, float bonus) {
    if (!std::isfinite(normal) || !std::isfinite(bonus) || normal < 0 || bonus < 0)
        return StaminaStatus::Unavailable;
    return normal > 0 || bonus > 0 ? StaminaStatus::Available : StaminaStatus::Empty;
}
inline bool releaseProtectionActive(std::uint64_t now, std::uint64_t until) {
    return until && now < until && until - now <= kRecallReleaseProtectionNs;
}
} // namespace self_recall::pure
