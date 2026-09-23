#pragma once

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace linked_stick::pure {

// TotK 1.2.1 Ridable copies 0x48 bytes; field proof is in the stage ledger.
inline constexpr std::size_t kRiderInputRecordSize = 0x48;
inline constexpr std::size_t kRiderInputForwardOffset = 0x08;
inline constexpr std::size_t kRiderInputSteeringOffset = 0x0C;

struct RiderInputValues {
    std::uint32_t forwardBits = 0;
    std::uint32_t steeringBits = 0;
    bool readable = false;
    bool finite = false;
};

inline RiderInputValues readRiderInputValues(const void* output) {
    RiderInputValues values{};
    if (!output) return values;

    const auto* bytes = static_cast<const std::byte*>(output);
    std::memcpy(&values.forwardBits, bytes + kRiderInputForwardOffset,
                sizeof(values.forwardBits));
    std::memcpy(&values.steeringBits, bytes + kRiderInputSteeringOffset,
                sizeof(values.steeringBits));
    values.readable = true;
    values.finite = std::isfinite(std::bit_cast<float>(values.forwardBits)) &&
                    std::isfinite(
                        std::bit_cast<float>(values.steeringBits));
    return values;
}

inline constexpr bool sameRiderInput(const RiderInputValues& left,
                                     const RiderInputValues& right) {
    return left.readable && right.readable && left.finite && right.finite &&
           left.forwardBits == right.forwardBits &&
           left.steeringBits == right.steeringBits;
}

inline constexpr std::uint64_t missingTicksBetween(std::uint64_t previous,
                                                   std::uint64_t current) {
    return previous && current > previous ? current - previous - 1 : 0;
}

}
