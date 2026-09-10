
#pragma once

#include <cstddef>

namespace totk::core {

template <std::size_t Capacity>
struct FixedString {
    static_assert(Capacity >= 2, "FixedString needs room for text and a terminator");

    char bytes[Capacity]{};

    constexpr void clear() { bytes[0] = '\0'; }

    constexpr void assign(const char* source) {
        clear();
        if (!source) return;
        std::size_t index = 0;
        while (index + 1 < Capacity && source[index] != '\0') {
            bytes[index] = source[index];
            ++index;
        }
        bytes[index] = '\0';
    }

    [[nodiscard]] constexpr const char* c_str() const { return bytes; }

    [[nodiscard]] constexpr bool equals(const char* other) const {
        if (!other) return false;
        for (std::size_t index = 0; index < Capacity; ++index) {
            if (bytes[index] != other[index]) return false;
            if (bytes[index] == '\0') return true;
        }
        return false;
    }
};

using ActorName = FixedString<64>;

} // namespace totk::core
