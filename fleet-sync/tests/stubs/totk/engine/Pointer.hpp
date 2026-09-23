#pragma once
#include <cstdint>
#include <cstring>
namespace totk::engine {
inline bool isPlausibleAddress(std::uintptr_t address) { return address >= 0x1000 && !(address & 7); }
template <class T> T readMemory(std::uintptr_t address) {
    T out{};
    std::memcpy(&out, reinterpret_cast<const void*>(address), sizeof(T));
    return out;
}
}
