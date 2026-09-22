// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "FlightClock.hpp"
#include "../program/modules/arrowbound/HookshotLog.hpp"
#include <lib.hpp>
#include <cstring>

namespace arrowbound::game_clock {
namespace {
std::uintptr_t base{};
pure::FlightClock clock;
pure::FlightMailbox<pure::FlightTime> published;
std::uint64_t faults{};
template<class T> T read(const void* pointer, std::size_t offset = 0) {
    T value;
    std::memcpy(&value, static_cast<const char*>(pointer) + offset, sizeof(value));
    return value;
}
HOOK_DEFINE_TRAMPOLINE(FlightFrameTimeHook) {
    static void Callback(void* module, const void* pauseContext, float scale) {
        Orig(module, pauseContext, scale);
        const auto* holder = read<const void*>(reinterpret_cast<const void*>(base + 0x0462E038));
        const auto* system = holder ? read<const void*>(holder) : nullptr;
        const auto* time = system ? read<const void*>(system, 0xC8) : nullptr;
        const auto value = clock.update(scale, time && (read<std::uint32_t>(time, 0x18) & 1), time);
        const bool sent = published.publish(value);
        if (!sent || value.status == pure::ClockStatus::Unavailable || value.status == pure::ClockStatus::Invalid) {
            if (++faults <= 4 || faults % 300 == 0)
                ZHLOG("FLIGHT_CLOCK_FAULT status=%u published=%u serial=%llu",
                    unsigned(value.status), unsigned(sent), (unsigned long long)value.serial);
        }
    }
};
}
void install(std::uintptr_t mainBase) {
    base = mainBase;
    constexpr auto offset = 0x007EDC00;
    const auto actual = read<std::uint32_t>(reinterpret_cast<const void*>(base + offset));
    if (actual != 0xF9400828) {
        ZHLOG("HOOK DISABLED flight clock word=%08x expected=f9400828", actual);
        return;
    }
    FlightFrameTimeHook::InstallAtOffset(offset);
}
bool snapshot(pure::FlightTime& out) { return published.snapshot(out); }
}
