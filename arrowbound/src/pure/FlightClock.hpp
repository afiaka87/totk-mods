// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace arrowbound::pure {
// Adapted from Self Recall's GameTime/FrameMailbox; native scales are 30 Hz units.
enum class ClockStatus { Unavailable, Running, Paused, Invalid };
struct FlightTime {
    std::uint64_t serial{}, nanoseconds{};
    float scale{};
    ClockStatus status{ClockStatus::Unavailable};
};
class FlightClock {
    FlightTime value_{};
    double remainder_{};
public:
    FlightTime update(float scale, bool paused, bool available) {
        if (value_.serial == UINT64_MAX) {
            value_.status = ClockStatus::Invalid;
            return value_;
        }
        ++value_.serial;
        value_.scale = scale;
        if (!available) { value_.status = ClockStatus::Unavailable; return value_; }
        if (!std::isfinite(scale) || scale < 0 || scale > INT32_MAX / 2.0) {
            value_.status = ClockStatus::Invalid;
            return value_;
        }
        if (paused || scale == 0) { value_.status = ClockStatus::Paused; return value_; }
        const double exact = double(scale) * (1000000000.0 / 30.0) + remainder_;
        const auto delta = static_cast<std::uint64_t>(exact + 0.000001);
        if (UINT64_MAX - value_.nanoseconds < delta) {
            value_.status = ClockStatus::Invalid;
            return value_;
        }
        value_.nanoseconds += delta;
        remainder_ = exact - double(delta);
        if (remainder_ < 0) remainder_ = 0;
        value_.status = ClockStatus::Running;
        return value_;
    }
};
template<class T> class FlightMailbox {
    static_assert(std::is_trivially_copyable_v<T>);
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    T value_{};
public:
    bool publish(const T& value) {
        if (busy_.test_and_set(std::memory_order_acquire)) return false;
        value_ = value;
        busy_.clear(std::memory_order_release);
        return true;
    }
    bool snapshot(T& value) {
        if (busy_.test_and_set(std::memory_order_acquire)) return false;
        value = value_;
        busy_.clear(std::memory_order_release);
        return true;
    }
};
class FlightClockCursor {
    FlightTime previous_{};
public:
    bool step(const FlightTime& now, float& seconds) {
        seconds = 0;
        if (!now.serial || now.status == ClockStatus::Unavailable ||
            now.status == ClockStatus::Invalid || now.serial < previous_.serial ||
            now.nanoseconds < previous_.nanoseconds) return false;
        if (now.serial == previous_.serial) return true;
        if (previous_.serial && now.status == ClockStatus::Running)
            seconds = float(double(now.nanoseconds - previous_.nanoseconds) / 1000000000.0);
        previous_ = now;
        return true;
    }
};
}
