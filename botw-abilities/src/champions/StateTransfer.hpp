// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "SavedState.hpp"

namespace botw_abilities::champions {

enum class StateTransfer { Ready, NotReady, MissingField, InvalidState, QueueUnavailable };

template <class Keys, class Reader>
StateTransfer readStates(const Keys& keys, Reader& reader, States& output) {
    States candidate{};
    for (std::size_t i = 0; i < kBlessingCount; ++i) {
        std::int32_t charges = 0;
        auto& state = candidate[i];
        if (!reader.boolean(keys[i].enabled, state.enabled) ||
            !reader.integer(keys[i].charges, charges) ||
            !reader.real(keys[i].recharge, state.remainingSeconds)) return StateTransfer::MissingField;
        if (charges < 0 || charges > kRules[i].charges) return StateTransfer::InvalidState;
        state.charges = static_cast<std::uint8_t>(charges);
    }
    if (!valid(candidate)) return StateTransfer::InvalidState;
    output = candidate;
    return StateTransfer::Ready;
}

template <class Keys, class Writer>
StateTransfer writeStates(const Keys& keys, Writer& writer, const States& committed,
                          const SavedSnapshot& snapshot, bool liveWorker) {
    if (!liveWorker || !snapshot.ready) return StateTransfer::NotReady;
    if (!valid(snapshot.states)) return StateTransfer::InvalidState;
    std::array<int, 3> pushes{};
    for (std::size_t i = 0; i < kBlessingCount; ++i) {
        pushes[0] += committed[i].enabled != snapshot.states[i].enabled;
        pushes[1] += committed[i].charges != snapshot.states[i].charges;
        pushes[2] += committed[i].remainingSeconds != snapshot.states[i].remainingSeconds;
    }
    if (!writer.room(pushes)) return StateTransfer::QueueUnavailable;
    for (std::size_t i = 0; i < kBlessingCount; ++i) {
        const auto& desired = snapshot.states[i];
        if (committed[i].enabled != desired.enabled) writer.boolean(keys[i].enabled, desired.enabled);
        if (committed[i].charges != desired.charges) writer.integer(keys[i].charges, desired.charges);
        if (committed[i].remainingSeconds != desired.remainingSeconds)
            writer.real(keys[i].recharge, desired.remainingSeconds);
    }
    return StateTransfer::Ready;
}

} // namespace botw_abilities::champions
