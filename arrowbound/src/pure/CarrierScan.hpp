// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace arrowbound::pure {

enum class CarrierPresence : std::uint8_t { Unknown, Absent, Present };

inline bool carrierGrantDue(CarrierPresence presence, std::uint64_t tick,
                            std::uint64_t lastAttempt, std::uint64_t retryTicks) {
    return presence == CarrierPresence::Absent &&
           (lastAttempt == 0 || tick - lastAttempt >= retryTicks);
}

template <class ReadName, class IsCarrier>
CarrierPresence scanCarrier(std::uint32_t count, ReadName read, IsCarrier matches) {
    // An empty loading pouch is not evidence that the emblem is missing.
    if (count == 0 || count > 512) return CarrierPresence::Unknown;
    for (std::uint32_t i = 0; i < count; ++i) {
        const char* name = read(i);
        if (!name) return CarrierPresence::Unknown;
        if (matches(name)) return CarrierPresence::Present;
    }
    return CarrierPresence::Absent;
}

}  // namespace arrowbound::pure
