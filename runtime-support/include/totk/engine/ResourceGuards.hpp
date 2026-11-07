#pragma once
// Shared "this resource does not drain" registry.
//
// Sibling of MovementScalars.hpp, and it exists for the same reason: several
// engine sites that spend a resource can each be hooked ONCE, and more than one
// feature wants to influence them. Whichever module owns a site consumes the
// guard published here, and any feature may publish without knowing who else is
// involved.
//
// The difference from MovementScalars is only the value type: these are
// booleans, because "never drains" has no useful magnitude. Publishing false
// must leave the engine value bit-for-bit untouched.
//
// Ownership rule (documented per guard by its consumer): exactly ONE module may
// consume a given guard, otherwise the same instruction would be hooked twice.
//
// Header-only, allocation-free, lock-free: a gameplay-thread publisher can run
// while a load-time inline hook reads.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace totk::guards {

enum class Guard : std::uint8_t {
    PlayerStamina,  // consumed by the stamina apply site
    ZonaiBattery,   // consumed by the two battery stores
    Arrows,         // consumed by the arrow consume-delta site
    ZonaiDevices,   // consumed by the Zonai device consume-delta site
    HorseStamina,   // consumed by the horse stamina decrement
    ClimbGrip,      // consumed by the climb slip decision
    Count,
};

namespace detail {

inline constexpr std::size_t kGuardCount = static_cast<std::size_t>(Guard::Count);

inline std::atomic<bool> g_active[kGuardCount] = {false, false, false,
                                                  false, false, false};

inline std::atomic<bool> g_available[kGuardCount] = {false, false, false,
                                                     false, false, false};

static_assert(std::atomic<bool>::is_always_lock_free,
              "resource guards are read from a load-time inline hook");

// Tripwire: adding a Guard without extending both initialiser lists above
// would leave the new guard reading a default-constructed atomic.
static_assert(kGuardCount == 6,
              "extend g_active and g_available when adding a Guard");

inline std::size_t index(Guard guard) { return static_cast<std::size_t>(guard); }

}  // namespace detail

// Publishing side (feature policy).
inline void publishActive(Guard guard, bool active) {
    detail::g_active[detail::index(guard)].store(active,
                                                 std::memory_order_release);
}

// Consuming side (the module that owns the engine site).
[[nodiscard]] inline bool active(Guard guard) {
    return detail::g_active[detail::index(guard)].load(
        std::memory_order_acquire);
}

// The site owner reports whether its guarded hook actually installed, so a
// feature can present the guard as unavailable instead of silently doing
// nothing.
inline void markAvailable(Guard guard, bool available) {
    detail::g_available[detail::index(guard)].store(available,
                                                    std::memory_order_release);
}

[[nodiscard]] inline bool available(Guard guard) {
    return detail::g_available[detail::index(guard)].load(
        std::memory_order_acquire);
}

}  // namespace totk::guards
