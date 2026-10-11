// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace cryonis::input {
constexpr std::uint64_t A = 1ull, B = 2ull, X = 4ull, StickR = 1ull << 5, L = 1ull << 6;
// Keep consumed holds out of native actions until a fresh physical release.
struct AimButtons {
    std::uint64_t held{};
    std::uint64_t mask(bool aiming, std::uint64_t buttons, bool fresh) {
        constexpr auto owned = A | B;
        if (fresh)
            held = (held & buttons) | (aiming ? (buttons & owned) : 0);
        return held | (aiming ? owned : 0);
    }
};
struct Wheel {
    std::uint32_t epoch{};
    bool open{}, amiibo{}, exited{}, cancelled{}, closed{};
};
inline std::uint64_t pack(Wheel w) {
    return (std::uint64_t(w.epoch) << 32) | unsigned(w.open) | (unsigned(w.amiibo) << 1) |
           (unsigned(w.exited) << 2) | (unsigned(w.cancelled) << 3) | (unsigned(w.closed) << 4);
}
inline Wheel unpack(std::uint64_t value) {
    return {std::uint32_t(value >> 32),
            bool(value & 1),
            bool(value & 2),
            bool(value & 4),
            bool(value & 8),
            bool(value & 16)};
}
// Native UI receives physical L; only the remembered gameplay tap is consumed.
struct RecentAbility {
    bool remembered{}, tap{};
    std::uint32_t epoch{}, acceptedEpoch{};
    void cancelTap() { tap = false; }
    void remember() {
        remembered = true;
        tap = false;
    }
    bool nativeAccepted() {
        const bool owned = remembered || tap;
        remembered = false;
        tap = false;
        return owned;
    }
    bool step(Wheel wheel, std::uint64_t buttons, std::uint64_t pressed, bool fresh, bool running,
              bool available, bool chordOwnsL) {
        if (wheel.exited && !wheel.cancelled && wheel.epoch != acceptedEpoch) {
            acceptedEpoch = wheel.epoch;
            nativeAccepted();
        }
        if (!running || !available || chordOwnsL) {
            tap = false;
            return false;
        }
        if (tap && (wheel.open || wheel.epoch != epoch))
            tap = false;
        if (!fresh)
            return false;
        if (pressed & B)
            tap = false;
        if (remembered && (pressed & L) && !(buttons & B) && !wheel.open && !chordOwnsL) {
            tap = true;
            epoch = wheel.epoch;
        }
        if (tap && !(buttons & L)) {
            tap = false;
            return remembered;
        }
        return false;
    }
};
inline constexpr std::uint64_t kMappedL = 1ull << 17; // ExecutePlayerCalcKey 1.2.1.
inline std::uint64_t recentGameplayKeys(std::uint64_t keys, bool owned) {
    return owned ? keys & ~kMappedL : keys;
}
// Native Amiibo update state0 is aiming; states1/2 are scan/drop processing.
inline bool cancelNativeAmiibo(bool cryonisOwned, bool running, bool samePlayer, unsigned state) {
    return cryonisOwned && running && samePlayer && state == 0;
}
inline std::uint32_t nativeCancelFlags(std::uint32_t flags) {
    return (flags & 0x3fffffffu) | 0x80000000u;
}
enum class Event {
    None,
    Requested,
    Activated,
    Unavailable,
    StaleWheel,
    NotCancelled,
    UserCancelled,
    TimedOut
};
struct Decision {
    std::uint64_t mask{}, add{};
    Event event{};
    bool beginAim{};
};
struct Activation {
    std::uint32_t pendingEpoch{};
    unsigned samples{};
    std::uint64_t lastButtons{};
    bool suppressL{}, suppressClick{};
    void reset() { *this = {}; }
    Decision step(Wheel wheel, std::uint64_t buttons, std::uint64_t pressed, bool fresh,
                  bool running, bool available) {
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
            else if (fresh && ++samples > 240)
                out.event = Event::TimedOut;
            else if (wheel.closed && wheel.cancelled && running) {
                out.event = available ? Event::Activated : Event::Unavailable;
                out.beginAim = available;
            }
            if (wheel.exited || wheel.closed)
                suppressL = (buttons & L) != 0 || suppressL;
            if (out.event != Event::None) {
                pendingEpoch = 0;
            }
        } else if (fresh && (pressed & StickR) && (buttons & L) && !(buttons & B) && !suppressL &&
                   !suppressClick && wheel.epoch && wheel.open && wheel.amiibo && !wheel.exited) {
            suppressClick = true;
            if (available) {
                pendingEpoch = wheel.epoch;
                samples = 0;
                out.event = Event::Requested;
            } else
                out.event = Event::Unavailable;
        }
        if (pendingEpoch) {
            out.mask |= A | B | StickR;
            // Keep L through native B cancellation, then consume it until physical release.
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
}
