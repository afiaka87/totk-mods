// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombReference.hpp"
#include <cstdint>

namespace remote_bomb::input {
inline constexpr std::uint64_t B = 1ull << 1, StickL = 1ull << 4,
                               StickR = 1ull << 5, L = 1ull << 6;
inline constexpr std::uint64_t kMappedAbilityKey = 1ull << 17;
struct Wheel {
    std::uint32_t epoch{};
    bool open{}, map{}, exited{}, cancelled{}, closed{};
};
inline constexpr std::uint64_t pack(Wheel wheel) {
    return (std::uint64_t(wheel.epoch) << 32) | unsigned(wheel.open) |
           (unsigned(wheel.map) << 1) | (unsigned(wheel.exited) << 2) |
           (unsigned(wheel.cancelled) << 3) | (unsigned(wheel.closed) << 4);
}
inline constexpr Wheel unpack(std::uint64_t value) {
    return {std::uint32_t(value >> 32), bool(value & 1), bool(value & 2),
            bool(value & 4), bool(value & 8), bool(value & 16)};
}
enum class Event { None, RequestWheelCancel, Equipped, NativeTakeover, UserCancelled,
                   StaleWheel, WheelAccepted, TimedOut, Unavailable, Use };
struct State {
    Shape selected = Shape::Sphere;
    Shape requested = Shape::Sphere;
    std::uint32_t pendingEpoch{}, tapEpoch{}, acceptedEpoch{};
    std::uint64_t consumed{}, lastButtons{};
    unsigned pendingSamples{};
    bool remembered{}, tap{}, reclaimed{};
    // L was just handed back to another mod's ability; the game's own stays blocked until L is let go.
    bool handing{};
};
struct Sample {
    Wheel wheel;
    std::uint64_t buttons{}, pressed{};
    bool fresh{}, running{}, available{}, nativeAccepted{};
    bool bombOut{};
    // The latest choice went to another mod's ability rather than a native one.
    bool modChosen{};
};
struct Decision {
    State next;
    Event event{};
    std::uint64_t mask{}, add{};
    bool use{};
};

inline Decision transition(State previous, Sample sample) {
    Decision result{previous};
    auto &next = result.next;
    const auto wheel = sample.wheel;
    if (sample.fresh) {
        next.lastButtons = sample.buttons;
        next.consumed &= sample.buttons;
    } else {
        sample.buttons = next.lastButtons;
        sample.pressed = 0;
    }
    if (!(sample.buttons & L) || !sample.running)
        next.handing = false;
    const bool accepted = sample.nativeAccepted ||
        (wheel.exited && !wheel.cancelled && wheel.epoch != next.acceptedEpoch);
    if (accepted) {
        next.acceptedEpoch = wheel.epoch;
        next.remembered = next.tap = next.reclaimed = next.handing = false;
        next.pendingEpoch = 0;
        result.event = Event::NativeTakeover;
    } else if (next.pendingEpoch) {
        if (wheel.epoch != next.pendingEpoch)
            result.event = Event::StaleWheel;
        else if (sample.fresh && (sample.pressed & B))
            result.event = Event::UserCancelled;
        else if (wheel.closed && !wheel.cancelled)
            result.event = Event::WheelAccepted;
        else if (sample.fresh && ++next.pendingSamples > 240)
            result.event = Event::TimedOut;
        else if (wheel.closed && wheel.cancelled && sample.running) {
            result.event = sample.available ? Event::Equipped : Event::Unavailable;
            if (sample.available) {
                next.selected = next.requested;
                next.remembered = true;
                next.reclaimed = false;
            }
        }
        if (wheel.exited || wheel.closed)
            next.consumed |= sample.buttons & L;
        if (result.event != Event::None)
            next.pendingEpoch = 0;
    } else if (sample.fresh && wheel.open && wheel.map && !wheel.exited && wheel.epoch &&
               (sample.buttons & L) && !(sample.buttons & B) && !(next.consumed & L)) {
        const auto sticks = sample.pressed & (StickL | StickR);
        if ((sticks == StickL || sticks == StickR) && !(next.consumed & sticks)) {
            next.consumed |= sticks;
            next.tap = false;
            if (sample.available) {
                next.requested = sticks == StickL ? Shape::Cube : Shape::Sphere;
                next.pendingEpoch = wheel.epoch;
                next.pendingSamples = 0;
                result.event = Event::RequestWheelCancel;
            } else
                result.event = Event::Unavailable;
        }
    }
    // A bomb left out after a native wheel choice still answers quick L. L stays with the bomb through
    // its blast; the next press with no bomb out hands L back and starts the chosen ability.
    const bool pressedL = sample.fresh && sample.running && sample.available && !next.pendingEpoch &&
        !wheel.open && (sample.pressed & L) && !(next.consumed & L) && !(sample.buttons & B);
    if (pressedL && sample.bombOut && !next.remembered)
        next.remembered = next.reclaimed = true;
    else if (pressedL && !sample.bombOut && next.reclaimed && !next.tap) {
        next.remembered = next.reclaimed = false;
        next.handing = sample.modChosen;
    }
    if (!sample.running || !sample.available || next.pendingEpoch ||
        (next.consumed & L) || wheel.open || (sample.pressed & B))
        next.tap = false;
    if (next.tap && wheel.epoch != next.tapEpoch)
        next.tap = false;
    if (sample.fresh && sample.running && sample.available && next.remembered &&
        !next.pendingEpoch && !wheel.open && !(next.consumed & L) && !(sample.buttons & B)) {
        if (sample.pressed & L) {
            next.tap = true;
            next.tapEpoch = wheel.epoch;
        }
        if (next.tap && !(sample.buttons & L)) {
            next.tap = false;
            result.use = true;
            result.event = Event::Use;
        }
    }
    result.mask = next.consumed;
    if (next.pendingEpoch) {
        result.mask |= B | StickL | StickR;
        if (wheel.open && !wheel.exited)
            result.add |= B;
    }
    return result;
}
// Whether the game's own ability is kept from L and from its ability check.
inline constexpr bool blocksNative(const State &state) { return state.remembered || state.handing; }
inline constexpr std::uint64_t gameplayKeys(std::uint64_t keys, bool remembered) {
    return remembered ? keys & ~kMappedAbilityKey : keys;
}
inline constexpr bool cancelNativeAmiibo(bool owned, bool running, bool samePlayer, unsigned state) {
    return owned && running && samePlayer && state == 0;
}
inline constexpr std::uint32_t nativeCancelFlags(std::uint32_t flags) {
    return (flags & 0x3fffffffu) | 0x80000000u;
}
}
