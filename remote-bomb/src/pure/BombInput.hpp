// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace remote_bomb::input {
inline constexpr std::uint64_t B = 1ull << 1, L = 1ull << 6;
inline constexpr std::uint64_t kMappedAbilityKey = 1ull << 17;
struct Wheel {
    std::uint32_t epoch{};
    bool open{}, exited{}, cancelled{}, closed{};
};
inline constexpr std::uint64_t pack(Wheel wheel) {
    return (std::uint64_t(wheel.epoch) << 32) | unsigned(wheel.open) |
           (unsigned(wheel.exited) << 1) | (unsigned(wheel.cancelled) << 2) |
           (unsigned(wheel.closed) << 3);
}
inline constexpr Wheel unpack(std::uint64_t value) {
    return {std::uint32_t(value >> 32), bool(value & 1), bool(value & 2), bool(value & 4),
            bool(value & 8)};
}
enum class Event { None, NativeTakeover, Use };
struct State {
    std::uint32_t tapEpoch{}, acceptedEpoch{};
    std::uint64_t lastButtons{};
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
    bool use{};
};

inline Decision transition(State previous, Sample sample) {
    Decision result{previous};
    auto &next = result.next;
    const auto wheel = sample.wheel;
    if (sample.fresh) {
        next.lastButtons = sample.buttons;
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
        result.event = Event::NativeTakeover;
    }
    // A bomb left out after a native wheel choice still answers quick L. L stays with the bomb through
    // its blast; the next press with no bomb out hands L back and starts the chosen ability.
    const bool pressedL = sample.fresh && sample.running && sample.available &&
        !wheel.open && (sample.pressed & L) && !(sample.buttons & B);
    if (pressedL && sample.bombOut && !next.remembered)
        next.remembered = next.reclaimed = true;
    else if (pressedL && !sample.bombOut && next.reclaimed && !next.tap) {
        next.remembered = next.reclaimed = false;
        next.handing = sample.modChosen;
    }
    if (!sample.running || !sample.available || wheel.open || (sample.pressed & B))
        next.tap = false;
    if (next.tap && wheel.epoch != next.tapEpoch)
        next.tap = false;
    if (sample.fresh && sample.running && sample.available && next.remembered &&
        !wheel.open && !(sample.buttons & B)) {
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
