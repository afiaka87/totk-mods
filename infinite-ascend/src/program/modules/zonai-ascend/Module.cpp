#include "Module.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace zonai_ascend {
namespace {

using SetPosAndScaleFn = void (*)(void* handle, const void* position,
                                  const void* scale);

bool g_leniencyHookHealthy = false;
bool g_markerScaleHooksHealthy = false;
SetPosAndScaleFn g_setPosAndScale = nullptr;
std::ptrdiff_t g_actorPositionOffset = 0;
// Post-calc publishes this snapshot before native marker callbacks and clears it afterward.
struct MarkerState {
    std::atomic<std::uintptr_t> owner{0};
    float playerPosition[3]{};
    bool playerValid = false;
};
constinit MarkerState g_marker;
constexpr std::ptrdiff_t kQueryReasonOffset = 0x7C;

float readFloat(const void* base, std::ptrdiff_t offset) {
    float value = 0.0f;
    __builtin_memcpy(&value,
                     static_cast<const std::uint8_t*>(base) + offset,
                     sizeof(value));
    return value;
}

bool plausibleCoordinate(float value) {
    return value == value && value > -100000.0f &&
           value < 100000.0f;
}

bool readPlayerPosition(const void* updateContext, float (&position)[3]) {
    void* actor = nullptr;
    __builtin_memcpy(&actor, static_cast<const std::uint8_t*>(updateContext) + 8,
                     sizeof(actor));
    if (!actor) return false;
    const float x = readFloat(actor, g_actorPositionOffset);
    const float y = readFloat(actor, g_actorPositionOffset + 4);
    const float z = readFloat(actor, g_actorPositionOffset + 8);
    if (!plausibleCoordinate(x) || !plausibleCoordinate(y) || !plausibleCoordinate(z))
        return false;
    position[0] = x;
    position[1] = y;
    position[2] = z;
    return true;
}

bool ownsMarker(std::uintptr_t owner, const void* handle) {
    const auto candidate = reinterpret_cast<std::uintptr_t>(handle);
    return candidate == owner + 0x3C || candidate == owner + 0x4C ||
           candidate == owner + 0x5C || candidate == owner + 0x6C;
}

}

void init(std::uintptr_t mainBase, bool leniencyHookHealthy,
          bool markerScaleHooksHealthy, std::ptrdiff_t setPosAndScaleOffset,
          std::ptrdiff_t actorPositionOffset) {
    g_leniencyHookHealthy = leniencyHookHealthy;
    g_markerScaleHooksHealthy = markerScaleHooksHealthy;
    g_setPosAndScale = nullptr;
    g_actorPositionOffset = actorPositionOffset;
    if (markerScaleHooksHealthy) {
        g_setPosAndScale = reinterpret_cast<SetPosAndScaleFn>(
            mainBase + setPosAndScaleOffset);
    }
}

bool resolveQueryValid(void* manager, bool nativePassed) {
    if (nativePassed) return true;
    if (!g_leniencyHookHealthy || !manager) return false;

    std::uint16_t reason = 0;
    __builtin_memcpy(&reason,
                     static_cast<std::uint8_t*>(manager) + kQueryReasonOffset,
                     sizeof(reason));
    if (!pure::canRelaxQueryFailure(reason)) return false;

    const std::uint16_t cleared = static_cast<std::uint16_t>(
        pure::clearLenientReasons(reason));
    __builtin_memcpy(static_cast<std::uint8_t*>(manager) + kQueryReasonOffset,
                     &cleared, sizeof(cleared));
    return true;
}

void beginMarkerPostCalc(void* manager, void* updateContext) {
    g_marker.owner.store(0, std::memory_order_release);
    g_marker.playerValid = false;
    if (!g_markerScaleHooksHealthy || !manager ||
        !updateContext || !g_setPosAndScale)
        return;

    if (!readPlayerPosition(updateContext, g_marker.playerPosition)) return;
    g_marker.playerValid = true;
    g_marker.owner.store(
        reinterpret_cast<std::uintptr_t>(manager),
        std::memory_order_release);
}

void endMarkerPostCalc(void* manager) {
    std::uintptr_t expected = reinterpret_cast<std::uintptr_t>(manager);
    g_marker.owner.compare_exchange_strong(
        expected, 0, std::memory_order_acq_rel);
    g_marker.playerValid = false;
}

void applyMarkerScale(void* handle, const void* position) {
    if (!handle || !position || !g_setPosAndScale ||
        !g_marker.playerValid)
        return;

    const std::uintptr_t owner =
        g_marker.owner.load(std::memory_order_acquire);
    if (!owner || !ownsMarker(owner, handle)) return;

    const float dx =
        readFloat(position, 0) - g_marker.playerPosition[0];
    const float dy =
        readFloat(position, 4) - g_marker.playerPosition[1];
    const float dz =
        readFloat(position, 8) - g_marker.playerPosition[2];
    if (!plausibleCoordinate(dx) ||
        !plausibleCoordinate(dy) ||
        !plausibleCoordinate(dz))
        return;

    const float distance = __builtin_sqrtf(dx * dx + dy * dy + dz * dz);
    const float scale = pure::markerScale(distance);
    const float uniformScale[3]{scale, scale, scale};
    g_setPosAndScale(handle, position, uniformScale);
}

}
