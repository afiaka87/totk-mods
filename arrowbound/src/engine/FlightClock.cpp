// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "FlightClock.hpp"
#include "FlightClockHook.hpp"
#include "../program/modules/arrowbound/HookshotLog.hpp"
#include <lib.hpp>
#include <arrowbound/ActiveGame.hpp>
#include <cstring>

namespace arrowbound::game_clock {
namespace {
std::uintptr_t base{};
std::ptrdiff_t physicsVariable{};
pure::FlightClock clock;
pure::FlightMailbox<pure::FlightTime> published;
std::uint64_t faults{};
template<class T> T read(const void* pointer, std::size_t offset = 0) {
    T value;
    std::memcpy(&value, static_cast<const char*>(pointer) + offset, sizeof(value));
    return value;
}
void tick(float scale) {
    const auto* system = read<const void*>(reinterpret_cast<const void*>(base + physicsVariable));
    const auto* time = system ? read<const void*>(system, 0xC8) : nullptr;
    const auto value = clock.update(scale, time && (read<std::uint32_t>(time, 0x18) & 1), time);
    const bool sent = published.publish(value);
    if (!sent || value.status == pure::ClockStatus::Unavailable || value.status == pure::ClockStatus::Invalid) {
        if (++faults <= 4 || faults % 300 == 0)
            ZHLOG("FLIGHT_CLOCK_FAULT status=%u published=%u serial=%llu",
                unsigned(value.status), unsigned(sent), (unsigned long long)value.serial);
    }
}
struct FlightFrameTimeHook {
    inline static FrameCallback previous{};
    static void Callback(void* module, const void* pauseContext, float scale) {
        previous(module, pauseContext, scale);
        tick(scale);
    }
};
// 1.4.x: the frame-rate update runs once per frame on both module-loop branches and stores the
// physics frame scale at +16. The pause bit it reads was written by the previous frame.
using RateCallback = void (*)(void*, float);
struct FrameRateHook {
    inline static RateCallback previous{};
    static void Callback(void* rate, float frames) {
        previous(rate, frames);
        tick(read<float>(rate, 16));
    }
};
}
void install(std::uintptr_t mainBase) {
    const auto* game = profiles::active();
    if (!game) return;
    base = mainBase;
    physicsVariable = game->variables.physics;
    const auto& rate = game->hooks.frameRate;
    if (rate.offset)
        installClockHook<RateCallback>(base + rate.offset, rate.word, FrameRateHook::Callback,
                                       FrameRateHook::previous);
    else
        installClockHook<FrameCallback>(base + game->hooks.flightClock.offset,
                                        game->hooks.flightClock.word,
                                        FlightFrameTimeHook::Callback, FlightFrameTimeHook::previous);
}
bool snapshot(pure::FlightTime& out) { return published.snapshot(out); }
}
