#include "Module.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ReachConfig.hpp"

namespace zonai_ascend {
namespace {

using SetPosAndScaleFn = void (*)(void* handle, const void* position,
                                  const void* scale);

bool g_leniencyHookHealthy = false;
bool g_markerScaleHooksHealthy = false;
SetPosAndScaleFn g_setPosAndScale = nullptr;
std::ptrdiff_t g_actorPositionOffset = 0;
std::atomic<std::uintptr_t> g_activeMarkerOwner{0};
float g_markerPlayerPos[3]{};
bool g_markerPlayerValid = false;

std::uint32_t floatToBits(float value) {
    std::uint32_t bits = 0;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    return bits;
}

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

std::uint32_t validationSpanBits() {
    constexpr auto reach = pure::deriveReach(pure::kReleaseReach);
    return floatToBits(reach.validationSpan);
}

float currentReach() {
    return pure::kReleaseReach;
}

float markerSpan() {
    constexpr auto reach = pure::deriveReach(pure::kReleaseReach);
    return reach.markerSpan;
}

bool resolveQueryValid(void* manager, bool nativePassed) {
    if (nativePassed) return true;
    if (!g_leniencyHookHealthy || !manager) return false;

    std::uint16_t reason = 0;
    __builtin_memcpy(&reason,
                     static_cast<std::uint8_t*>(manager) + 0x7C,
                     sizeof(reason));
    if (!pure::canRelaxQueryFailure(reason)) return false;

    const std::uint16_t cleared = static_cast<std::uint16_t>(
        pure::clearLenientReasons(reason));
    __builtin_memcpy(static_cast<std::uint8_t*>(manager) + 0x7C,
                     &cleared, sizeof(cleared));
    return true;
}

void beginMarkerPostCalc(void* manager, void* updateContext) {
    g_activeMarkerOwner.store(0, std::memory_order_release);
    g_markerPlayerValid = false;
    if (!g_markerScaleHooksHealthy || !manager ||
        !updateContext || !g_setPosAndScale)
        return;

    void* actor = nullptr;
    __builtin_memcpy(
        &actor,
        static_cast<const std::uint8_t*>(updateContext) + 8,
        sizeof(actor));
    if (!actor) return;

    const float px = readFloat(actor, g_actorPositionOffset);
    const float py = readFloat(actor, g_actorPositionOffset + 4);
    const float pz = readFloat(actor, g_actorPositionOffset + 8);
    if (!plausibleCoordinate(px) ||
        !plausibleCoordinate(py) ||
        !plausibleCoordinate(pz))
        return;

    g_markerPlayerPos[0] = px;
    g_markerPlayerPos[1] = py;
    g_markerPlayerPos[2] = pz;
    g_markerPlayerValid = true;
    g_activeMarkerOwner.store(
        reinterpret_cast<std::uintptr_t>(manager),
        std::memory_order_release);
}

void endMarkerPostCalc(void* manager) {
    std::uintptr_t expected = reinterpret_cast<std::uintptr_t>(manager);
    g_activeMarkerOwner.compare_exchange_strong(
        expected, 0, std::memory_order_acq_rel);
    g_markerPlayerValid = false;
}

void applyMarkerScale(void* handle, const void* position) {
    if (!handle || !position || !g_setPosAndScale ||
        !g_markerPlayerValid)
        return;

    const std::uintptr_t owner =
        g_activeMarkerOwner.load(std::memory_order_acquire);
    if (!owner) return;

    const std::uintptr_t candidate = reinterpret_cast<std::uintptr_t>(handle);
    if (candidate != owner + 0x3C &&
        candidate != owner + 0x4C &&
        candidate != owner + 0x5C &&
        candidate != owner + 0x6C)
        return;

    const float dx =
        readFloat(position, 0) - g_markerPlayerPos[0];
    const float dy =
        readFloat(position, 4) - g_markerPlayerPos[1];
    const float dz =
        readFloat(position, 8) - g_markerPlayerPos[2];
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
