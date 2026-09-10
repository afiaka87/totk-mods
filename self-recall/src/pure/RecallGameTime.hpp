#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace self_recall::pure {

enum class GameTimeStatus : std::uint8_t {
    Unavailable, Running, Paused, NoAdvance, InvalidDelta, Exhausted,
};

struct GameTimeSnapshot {
    std::uint64_t serial = 0;
    std::uint64_t elapsedNanoseconds = 0;
    std::uint32_t frameQuanta = 0;
    GameTimeStatus status = GameTimeStatus::Unavailable;
};

class GameTime {
public:
    GameTimeSnapshot update(float frameScale, bool paused, bool available = true) {
        if (snapshot_.status == GameTimeStatus::Exhausted || snapshot_.serial == UINT64_MAX) {
            snapshot_.status = GameTimeStatus::Exhausted;
            return snapshot_;
        }
        ++snapshot_.serial;
        snapshot_.frameQuanta = 0;
        if (!available) {
            snapshot_.status = GameTimeStatus::Unavailable;
            return snapshot_;
        }
        const double quanta = static_cast<double>(frameScale) * 2.0;
        if (!std::isfinite(quanta) || quanta < 0 || quanta > INT32_MAX ||
            quanta != std::floor(quanta)) {
            snapshot_.status = GameTimeStatus::InvalidDelta;
            return snapshot_;
        }
        snapshot_.frameQuanta = static_cast<std::uint32_t>(quanta);
        if (paused || !snapshot_.frameQuanta) {
            snapshot_.status = paused ? GameTimeStatus::Paused : GameTimeStatus::NoAdvance;
            return snapshot_;
        }
        const auto numerator = static_cast<std::uint64_t>(snapshot_.frameQuanta) *
                               1000000000ull + remainder_;
        const auto delta = numerator / 60;
        if (snapshot_.elapsedNanoseconds > UINT64_MAX - delta) {
            snapshot_.status = GameTimeStatus::Exhausted;
            return snapshot_;
        }
        snapshot_.elapsedNanoseconds += delta;
        remainder_ = static_cast<std::uint8_t>(numerator % 60);
        snapshot_.status = GameTimeStatus::Running;
        return snapshot_;
    }
private:
    GameTimeSnapshot snapshot_{};
    std::uint8_t remainder_ = 0;
};

inline constexpr std::uint64_t kRecallWindowNanoseconds = 64000000000ull;
inline constexpr std::uint64_t kRecallMinimumNanoseconds = 1500000000ull;

}  // namespace self_recall::pure
