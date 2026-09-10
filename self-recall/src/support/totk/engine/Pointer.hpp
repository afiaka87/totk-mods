
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace totk::engine {

inline constexpr std::uintptr_t kMinimumMappedAddress = 0x1000;
inline constexpr std::uintptr_t kMaximumMappedAddress = 1ULL << 40;

[[nodiscard]] constexpr bool isPlausibleAddress(std::uintptr_t address) {
    return address >= kMinimumMappedAddress && address < kMaximumMappedAddress &&
           (address & (alignof(std::uintptr_t) - 1)) == 0;
}

[[nodiscard]] constexpr bool isPlausibleStringAddress(std::uintptr_t address) {
    return address >= kMinimumMappedAddress && address < kMaximumMappedAddress;
}

template <class Value>
[[nodiscard]] inline Value readMemory(std::uintptr_t address) {
    Value value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(Value));
    return value;
}

template <class Value>
inline void writeMemory(std::uintptr_t address, const Value& value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(Value));
}

} // namespace totk::engine
