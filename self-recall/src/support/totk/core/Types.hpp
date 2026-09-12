#pragma once

#include <cstddef>
#include <cstdint>

namespace totk::core {

template <class Value, class Error>
struct [[nodiscard]] Result {
    Value value{};
    Error error{};
    bool succeeded = false;

    [[nodiscard]] static constexpr Result success(const Value& result) {
        return Result{result, Error{}, true};
    }

    [[nodiscard]] static constexpr Result failure(Error reason) {
        return Result{Value{}, reason, false};
    }

    [[nodiscard]] constexpr explicit operator bool() const { return succeeded; }
};

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

struct TickCount {
    std::uint64_t value = 0;

    [[nodiscard]] friend constexpr bool operator==(TickCount, TickCount) = default;
};

[[nodiscard]] constexpr std::uint64_t elapsedTicks(TickCount newer, TickCount older) {
    return newer.value >= older.value ? newer.value - older.value : 0;
}

struct DistanceMeters {
    float value = 0.0F;
};

struct SceneToken {
    std::uintptr_t value = 0;

    [[nodiscard]] constexpr bool isValid() const { return value != 0; }
    [[nodiscard]] friend constexpr bool operator==(SceneToken, SceneToken) = default;
};

struct SceneGeneration {
    std::uint32_t value = 0;

    [[nodiscard]] friend constexpr bool operator==(SceneGeneration, SceneGeneration) = default;
};

struct ImageOffset {
    std::ptrdiff_t value = 0;
};

struct WorldPosition {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct SurfaceNormal {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct RotationBasis {
    float values[9]{};
};

struct WorldTransform {
    RotationBasis rotation{};
    WorldPosition position{};
};

}
