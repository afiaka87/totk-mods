// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "SavedState.hpp"
#include <cstring>
#include <limits>

namespace botw_abilities::champions {

enum class SavePatch { Applied, NotReady, InvalidBuffer, MissingFields, WrongType, DuplicateField };

inline std::uint32_t saveWord(const unsigned char* bytes, std::size_t offset) {
    std::uint32_t word;
    std::memcpy(&word, bytes + offset, sizeof(word));
    return word;
}

// TotK 1.2.1 scalar records follow {0, type} section markers; values occupy the second word.
template <class Keys>
SavePatch patchSaveBuffer(unsigned char* bytes, std::size_t size,
                         const Keys& keys, const SavedSnapshot& snapshot) {
    if (!snapshot.ready || !valid(snapshot.states)) return SavePatch::NotReady;
    if (!bytes || size < 0x28 || size > 16 * 1024 * 1024 || saveWord(bytes, 0) != 0x01020304)
        return SavePatch::InvalidBuffer;
    const std::size_t end = saveWord(bytes, 8);
    if (end < 0x28 || end > size || (end - 0x20) % 8) return SavePatch::InvalidBuffer;
    std::array<std::array<std::size_t, 3>, kBlessingCount> offsets{};
    constexpr std::array<std::uint32_t, 3> types{0, 2, 4};
    auto type = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t at = 0x20; at < end; at += 8) {
        const auto hash = saveWord(bytes, at);
        if (!hash) { type = saveWord(bytes, at + 4); continue; }
        for (std::size_t i = 0; i < kBlessingCount; ++i) {
            const std::array hashes{keys[i].enabled, keys[i].charges, keys[i].recharge};
            for (std::size_t field = 0; field < hashes.size(); ++field) {
                if (hash != hashes[field]) continue;
                if (type != types[field]) return SavePatch::WrongType;
                if (offsets[i][field]) return SavePatch::DuplicateField;
                offsets[i][field] = at + 4;
            }
        }
    }
    for (const auto& fields : offsets)
        for (auto offset : fields)
            if (!offset) return SavePatch::MissingFields;
    for (std::size_t i = 0; i < kBlessingCount; ++i) {
        const auto& state = snapshot.states[i];
        const std::uint32_t enabled = state.enabled ? 1 : 0, charges = state.charges;
        std::memcpy(bytes + offsets[i][0], &enabled, sizeof(enabled));
        std::memcpy(bytes + offsets[i][1], &charges, sizeof(charges));
        std::memcpy(bytes + offsets[i][2], &state.remainingSeconds, sizeof(float));
    }
    return SavePatch::Applied;
}

} // namespace botw_abilities::champions
