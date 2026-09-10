#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace self_recall::pure {

inline constexpr std::size_t kMonochromeFilterBytes = 0x90;

enum class MonochromeFilterStatus : unsigned { Ready, InvalidSize, OverlappingBuffers };

inline MonochromeFilterStatus buildMonochromeFilter(
        std::span<const std::byte> native, std::span<std::byte> destination) {
    if (native.size() != kMonochromeFilterBytes ||
        destination.size() != kMonochromeFilterBytes ||
        !native.data() || !destination.data())
        return MonochromeFilterStatus::InvalidSize;
    const auto source = reinterpret_cast<std::uintptr_t>(native.data());
    const auto target = reinterpret_cast<std::uintptr_t>(destination.data());
    const auto distance = source > target ? source - target : target - source;
    if (distance < kMonochromeFilterBytes)
        return MonochromeFilterStatus::OverlappingBuffers;

    std::uint32_t flags;
    std::memcpy(&flags, native.data() + 0x64, sizeof(flags));
    flags = (flags | 1u) & ~0x10u;
    std::memcpy(destination.data(), native.data(), kMonochromeFilterBytes);
    std::memcpy(destination.data() + 0x64, &flags, sizeof(flags));
    const float complete = 1.0f;
    const float character = 0.375f;
    const float other = 0.25f;
    std::memcpy(destination.data() + 0x1C, &complete, sizeof(complete));
    std::memcpy(destination.data() + 0x58, &complete, sizeof(complete));
    std::memcpy(destination.data() + 0x60, &complete, sizeof(complete));
    std::memcpy(destination.data() + 0x7C, &character, sizeof(character));
    std::memcpy(destination.data() + 0x80, &other, sizeof(other));
    return MonochromeFilterStatus::Ready;
}

enum class ObjectAttributeStatus : unsigned { Ready, InvalidValue, EncodingFailed };

template<class Set, class Calculate>
void uploadMonochromeAttribute(float original, float excluded, Set set, Calculate calculate) {
    set(excluded);
    calculate();
    if (excluded != original) set(original);
}

inline ObjectAttributeStatus enableMonochromeExclusion(float source, float& destination) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    constexpr float maximum = 65535.0f / 255.0f;
    if (!std::isfinite(source) || source < 0.0f || source > maximum)
        return ObjectAttributeStatus::InvalidValue;
    const float scaled = source * 255.0f;
    const auto flags = static_cast<std::uint32_t>(scaled);
    if (flags & 0x80u) {
        destination = source;
        return ObjectAttributeStatus::Ready;
    }
    const auto excluded = flags | 0x80u;
    float encoded = static_cast<float>(excluded) / 255.0f;
    if (static_cast<std::uint32_t>(encoded * 255.0f) < excluded)
        encoded = std::nextafter(encoded, std::numeric_limits<float>::infinity());
    if (encoded > maximum || static_cast<std::uint32_t>(encoded * 255.0f) != excluded)
        return ObjectAttributeStatus::EncodingFailed;
    destination = encoded;
    return ObjectAttributeStatus::Ready;
}

} // namespace self_recall::pure
