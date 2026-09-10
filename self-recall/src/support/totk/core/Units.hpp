
#pragma once

#include <cstddef>
#include <cstdint>

namespace totk::core {

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

struct LinearVelocity {
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

} // namespace totk::core
