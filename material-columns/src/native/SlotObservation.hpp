// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "../pure/Identity.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace material_columns::native {
struct SlotObservation {
    std::uint64_t item{};
    std::int32_t quantity{};
    bool captured{};
    bool eligible{};
};

// setSlot's material parameter record: actor+0, quantity+12, pattern+24, invalid+64.
inline SlotObservation observeSlot(const void* parameters) {
    SlotObservation result{};
    if (!parameters) return result;
    result.captured = true;
    const auto* bytes = static_cast<const std::uint8_t*>(parameters);
    const char* actor = nullptr;
    std::uint32_t pattern = 0;
    std::memcpy(&actor, bytes, sizeof(actor));
    std::memcpy(&result.quantity, bytes + 12, sizeof(result.quantity));
    std::memcpy(&pattern, bytes + 24, sizeof(pattern));
    if (!actor || !*actor) return result;
    // GameData material names use String64. Never retain its transient pointer.
    std::size_t length = 0;
    while (length < 64 && actor[length]) ++length;
    if (length == 64) return result;
    result.item = identity(actor);
    result.eligible = bytes[9] && result.quantity > 0 && !bytes[64] && pattern == 0;
    return result;
}
// Compare consumed setSlot fields, not uninitialized native padding or the
// transient actor-name pointer. String64 identity is already used by the catalog.
struct SlotPresentation {
    std::array<std::uint8_t, 79> bytes{};
    explicit SlotPresentation(const void* parameters) {
        if (!parameters) return;
        const auto item = observeSlot(parameters).item;
        std::memcpy(bytes.data(), &item, sizeof(item));
        const auto* source = static_cast<const std::uint8_t*>(parameters);
        std::size_t destination = sizeof(item);
        for (const auto& range : {std::pair{8u, 2u}, std::pair{12u, 28u}, std::pair{41u, 1u},
                                std::pair{44u, 22u}, std::pair{68u, 5u}, std::pair{76u, 13u}}) {
            std::memcpy(bytes.data() + destination, source + range.first, range.second);
            destination += range.second;
        }
    }
    SlotPresentation() = default;
    bool operator==(const SlotPresentation& other) const { return bytes == other.bytes; }
};
}
