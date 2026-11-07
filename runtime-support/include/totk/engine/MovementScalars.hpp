#pragma once
// Generic movement-scalar registry.
//
// Several features want to multiply the same engine-computed movement values
// (jump height, sprint/swim speed, glide speed). The engine sites that carry
// those values can only be hooked ONCE each, so the hook and the policy that
// drives it must be separable: whichever module owns a site consumes the
// combined scale published here, and any feature may publish its own scale
// without knowing who else is involved.
//
// The registry is deliberately header-only and allocation-free. Its storage is
// a small array of lock-free atomics, so a publisher may run on the gameplay
// thread while a consumer reads inside a load-time inline hook.
//
// Scales are Q8 fixed point: 256 == vanilla. A consumer that reads exactly
// kUnitScaleQ8 must leave the engine value bit-for-bit untouched.
//
// Ownership rule (documented per axis by its consumer): exactly ONE module may
// consume a given axis, otherwise a published scale would be applied twice.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace totk::movement {

enum class Axis : std::uint8_t {
    JumpHeight,   // consumed by the jump-height clamp site
    SprintSpeed,  // consumed by the run/swim/swim-animation/acceleration stores
    GlideSpeed,   // consumed by the paraglider speed site
    ClimbSpeed,   // consumed by Ascent's climb-physics hook
    ClimbJump,    // consumed by the climb-lunge velocity site
    Count,
};

inline constexpr std::uint16_t kUnitScaleQ8 = 256;
inline constexpr float kQ8ToFloat = 1.0f / 256.0f;

namespace detail {

inline constexpr std::size_t kAxisCount = static_cast<std::size_t>(Axis::Count);

// Every axis must be named here. A missing initialiser would value-initialise
// to 0, and a consumer reading a scale of 0 would multiply the engine value
// away instead of leaving it alone.
inline std::atomic<std::uint16_t> g_scaleQ8[kAxisCount] = {
    kUnitScaleQ8,
    kUnitScaleQ8,
    kUnitScaleQ8,
    kUnitScaleQ8,
    kUnitScaleQ8,
};

inline std::atomic<bool> g_available[kAxisCount] = {false, false, false,
                                                    false, false};

static_assert(std::atomic<std::uint16_t>::is_always_lock_free,
              "movement scalars are read from a load-time inline hook");

// Tripwire: adding an Axis without extending both initialiser lists above
// would leave the new axis at scale 0. Update the lists, then this count.
static_assert(kAxisCount == 5,
              "extend g_scaleQ8 and g_available when adding an Axis");

inline std::size_t index(Axis axis) { return static_cast<std::size_t>(axis); }

}  // namespace detail

// Publishing side (feature policy).
inline void publishScaleQ8(Axis axis, std::uint16_t scaleQ8) {
    detail::g_scaleQ8[detail::index(axis)].store(scaleQ8,
                                                 std::memory_order_release);
}

// Consuming side (the module that owns the engine site).
[[nodiscard]] inline std::uint16_t scaleQ8(Axis axis) {
    return detail::g_scaleQ8[detail::index(axis)].load(std::memory_order_acquire);
}

[[nodiscard]] inline float scale(Axis axis) {
    return static_cast<float>(scaleQ8(axis)) * kQ8ToFloat;
}

[[nodiscard]] inline bool isVanilla(Axis axis) {
    return scaleQ8(axis) == kUnitScaleQ8;
}

// The site owner reports whether its guarded hook actually installed, so a
// feature can present the axis as unavailable instead of silently doing
// nothing.
inline void markAvailable(Axis axis, bool available) {
    detail::g_available[detail::index(axis)].store(available,
                                                   std::memory_order_release);
}

[[nodiscard]] inline bool available(Axis axis) {
    return detail::g_available[detail::index(axis)].load(
        std::memory_order_acquire);
}

}  // namespace totk::movement
