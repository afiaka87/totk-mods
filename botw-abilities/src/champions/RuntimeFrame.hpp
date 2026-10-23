// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "Locations.hpp"
#include "State.hpp"

namespace botw_abilities::champions {
inline constexpr unsigned kContextFlagShift = 40;
inline constexpr std::uint64_t kContextSceneMask = (1ull << kContextFlagShift) - 1;

// Only admitted gameplay is published; the low 40 bits retain the scene identity.
[[nodiscard]] constexpr std::uint64_t packContext(
    std::uint64_t scene, const std::array<Context, kBlessingCount>& contexts) noexcept {
    unsigned flags = 1 | (contexts[0].shrine ? 2 : 0) | (contexts[0].castle ? 4 : 0);
    for (std::size_t i = 0; i < kBlessingCount; ++i)
        if (contexts[i].ballad) flags |= 1u << (i + 3);
    return scene | (std::uint64_t(flags) << kContextFlagShift);
}

[[nodiscard]] constexpr std::uint64_t contextScene(std::uint64_t packed) noexcept {
    return packed & kContextSceneMask;
}

[[nodiscard]] constexpr Context unpackContext(std::uint64_t packed, Blessing blessing) noexcept {
    const auto flags = unsigned(packed >> kContextFlagShift);
    return {bool(flags & 1), bool(flags & 2),
            bool(flags & (1u << (unsigned(blessing) + 3))), bool(flags & 4)};
}

// A failed castle or vow read only loses that speed-up; it never disables the blessings.
template <class Ballad>
[[nodiscard]] constexpr std::array<Context, kBlessingCount> gameplayContexts(
    bool shrine, LocationResult castle, Ballad ballad) {
    std::array<Context, kBlessingCount> contexts{};
    for (std::size_t i = 0; i < kBlessingCount; ++i)
        contexts[i] = {true, shrine, bool(ballad(static_cast<Blessing>(i))), castle && castle.castle};
    return contexts;
}

[[nodiscard]] constexpr float simulationSeconds(std::uint64_t elapsed, bool sceneChanged) noexcept {
    return sceneChanged ? 0 : float(double(elapsed) / (4294967296.0 * 30.0));
}
} // namespace botw_abilities::champions
