#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace self_recall::profiles {

enum class Version : std::uint8_t {
    V100, V110, V112, V120, V121, V140, V141, V142, V143,
};

struct Site {
    std::ptrdiff_t offset;
    std::uint32_t word;
};

struct Build {
    Version version;
    const char* name;
    std::array<Site, 6> identity;
    std::ptrdiff_t physicsVariable;
    Site frameRate;
};

const Build* active();
const Build* activate(std::uintptr_t mainBase, std::size_t textSize);
std::ptrdiff_t address(std::ptrdiff_t baseline121);
bool newerRenderer();

inline constexpr std::size_t kBuildCount = 9;

// Hooks install only after activate() succeeds, so engine code indexes tables by the selected build.
template <class T, std::size_t N> const T& row(const T (&rows)[N]) {
    static_assert(N == kBuildCount);
    return rows[static_cast<std::size_t>(active()->version)];
}
template <class T, std::size_t N> const T& row(const std::array<T, N>& rows) {
    static_assert(N == kBuildCount);
    return rows[static_cast<std::size_t>(active()->version)];
}

// True when main+offset is inside .text and holds the expected instruction word.
inline bool holds(std::uintptr_t mainBase, std::size_t textSize, std::uintptr_t offset, std::uint32_t word) {
    return offset && offset <= textSize - sizeof(word) &&
           *reinterpret_cast<const std::uint32_t*>(mainBase + offset) == word;
}
template <std::size_t N>
bool holds(std::uintptr_t mainBase, std::size_t textSize, const std::array<std::uintptr_t, N>& offsets,
           const std::array<std::uint32_t, N>& words, std::size_t count = N) {
    for (std::size_t i = 0; i < count; ++i)
        if (!holds(mainBase, textSize, offsets[i], words[i])) return false;
    return true;
}

} // namespace self_recall::profiles
