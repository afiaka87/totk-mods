#pragma once
#include <cstddef>
#include <cstring>
#include <span>
#include <utility>

namespace self_recall::pure {
// The snapshot becomes the live backup until the second exchange restores it.
inline bool exchangeParameterBytes(std::span<std::byte> live, std::span<std::byte> snapshot) {
    if (live.size() != snapshot.size()) return false;
    for (std::size_t i = 0; i < live.size(); ++i) std::swap(live[i], snapshot[i]);
    return true;
}
} // namespace self_recall::pure
