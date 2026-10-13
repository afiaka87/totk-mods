// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

// The Camera hover + L + R3 chord. A build that offers Stasis on a wheel page turns it off.
#ifndef STASIS_WHEEL_CHORD
#define STASIS_WHEEL_CHORD 1
#endif

namespace stasis::input {
inline constexpr bool kWheelChord = STASIS_WHEEL_CHORD != 0;
constexpr std::uint64_t A = 1ull, B = 2ull, StickR = 1ull << 5, L = 1ull << 6;
struct Wheel {
    std::uint32_t epoch{};
    bool open{}, camera{}, exited{}, cancelled{}, closed{};
    bool chosen{};
};
inline std::uint64_t pack(Wheel w) {
    return (std::uint64_t(w.epoch) << 32) | unsigned(w.open) | (unsigned(w.camera) << 1) |
           (unsigned(w.exited) << 2) | (unsigned(w.cancelled) << 3) | (unsigned(w.closed) << 4) |
           (unsigned(w.chosen) << 5);
}
inline Wheel unpack(std::uint64_t value) {
    return {std::uint32_t(value >> 32),
            bool(value & 1),
            bool(value & 2),
            bool(value & 4),
            bool(value & 8),
            bool(value & 16),
            bool(value & 32)};
}
struct RecentAbility {
    bool remembered{}, tap{};
    std::uint32_t epoch{};
    void remember() { remembered = true; tap = false; }
    void reset() { *this = {}; }
    bool step(Wheel wheel, std::uint64_t buttons, std::uint64_t pressed, bool fresh,
              bool running, bool available, bool chordOwnsL) {
        if (!running || !available || chordOwnsL || wheel.open || wheel.epoch != epoch)
            tap = false;
        if (!fresh || !running || !available || chordOwnsL)
            return false;
        if (pressed & B)
            tap = false;
        if (remembered && (pressed & L) && !(buttons & B) && !wheel.open) {
            tap = true;
            epoch = wheel.epoch;
        }
        if (tap && !(buttons & L)) {
            tap = false;
            return remembered;
        }
        return false;
    }
    bool ownsNative(std::uintptr_t actor, std::uintptr_t player) const {
        return remembered && player && actor == player;
    }
};
inline bool canFreeze(bool frozen, float cooldown) { return !frozen && cooldown <= 0.0f; }
// Reads the ability wheel's item count, cursor and item IDs; ID 7 is the Camera.
template <class Reader> bool cameraSelected(std::uintptr_t screen, Reader read) {
    const auto count = read.template operator()<std::int32_t>(screen + 0x1A8);
    const auto cursor = read.template operator()<std::int32_t>(screen + 0x23C);
    if (count < 1 || count > 8 || cursor < 0 || cursor >= count)
        return false;
    if (read.template operator()<std::uint8_t>(screen + 0x244) ||
        !read.template operator()<std::uint8_t>(screen + 0x245))
        return false;
    return read.template operator()<std::uint32_t>(screen + 0x270 + 4 * cursor) == 7;
}
enum class Event {
    None,
    Requested,
    Activated,
    Unavailable,
    StaleWheel,
    NotCancelled,
    UserCancelled,
    TimedOut,
    Released,
    HoldLost
};
struct Decision {
    std::uint64_t mask{}, add{};
    Event event{};
    bool beginAim{}, releaseFreeze{};
};
struct Activation {
    std::uint32_t pendingEpoch{};
    std::uint32_t pendingGeneration{};
    // The last wheel whose page choice was taken; kept through reset so each choice acts once.
    std::uint32_t chosenEpoch{};
    unsigned samples{};
    std::uint64_t lastButtons{};
    bool suppressL{}, suppressClick{}, pendingRelease{};
    void reset() {
        const auto seen = chosenEpoch;
        *this = {};
        chosenEpoch = seen;
    }
    void request(std::uint32_t epoch, Decision &out, bool available, bool frozen,
                 std::uint32_t generation) {
        if (frozen || available) {
            pendingEpoch = epoch;
            samples = 0;
            out.event = Event::Requested;
            pendingRelease = frozen;
            pendingGeneration = generation;
        } else
            out.event = Event::Unavailable;
    }
    Decision step(Wheel wheel, std::uint64_t buttons, std::uint64_t pressed, bool fresh,
                  bool running, bool available, bool frozen = false, std::uint32_t generation = 0) {
        Decision out;
        if (fresh) {
            lastButtons = buttons;
            if (!(buttons & L))
                suppressL = false;
            if (!(buttons & StickR))
                suppressClick = false;
        } else
            buttons = lastButtons;
        if (pendingEpoch) {
            if (wheel.epoch != pendingEpoch)
                out.event = Event::StaleWheel;
            else if (fresh && (pressed & B))
                out.event = Event::UserCancelled;
            else if (wheel.exited && !wheel.cancelled)
                out.event = Event::NotCancelled;
            else if (wheel.closed && !wheel.exited)
                out.event = Event::NotCancelled;
            else if (pendingRelease && (!frozen || generation != pendingGeneration))
                out.event = Event::HoldLost;
            else if (fresh && ++samples > 240)
                out.event = Event::TimedOut;
            else if (wheel.closed && wheel.cancelled && running) {
                if (pendingRelease) {
                    out.event = Event::Released;
                    out.releaseFreeze = true;
                } else {
                    out.event = available ? Event::Activated : Event::Unavailable;
                    out.beginAim = available;
                }
            }
            if (wheel.exited || wheel.closed)
                suppressL = (buttons & L) != 0 || suppressL;
            if (out.event != Event::None) {
                pendingEpoch = 0;
                pendingRelease = false;
                pendingGeneration = 0;
            }
        } else if (wheel.chosen && wheel.exited && wheel.cancelled && wheel.epoch &&
                   wheel.epoch != chosenEpoch) {
            // Picked from a wheel page: the wheel is already closing, so no B is needed.
            chosenEpoch = wheel.epoch;
            request(wheel.epoch, out, available, frozen, generation);
        } else if (kWheelChord && fresh && (pressed & StickR) && (buttons & L) && !(buttons & B) &&
                   !suppressL && !suppressClick && wheel.epoch && wheel.open && wheel.camera &&
                   !wheel.exited) {
            suppressClick = true;
            request(wheel.epoch, out, available, frozen, generation);
        }
        if (pendingEpoch) {
            out.mask |= A | B | StickR;
            // B takes the game's normal cancel route. L is held through the cancel and then suppressed until released, so the Camera cannot be selected or reopened.
            if (wheel.open && !wheel.exited)
                out.add |= B;
        }
        if (suppressL)
            out.mask |= L;
        if (suppressClick)
            out.mask |= StickR;
        return out;
    }
};
} // namespace stasis::input
