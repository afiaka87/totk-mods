#include "Module.hpp"

#include <atomic>
#include <cstddef>
#include <cstring>

#include "ReachConfig.hpp"

namespace zonai_ascend {
namespace {

// HandleELink::setPosAndScale (TotK 1.2.1); the installer verifies its entry word first.
constexpr ptrdiff_t kSetPosAndScaleOffset = 0x01DAF314;

using SetPosAndScaleFn = void (*)(void* handle, const void* position,
                                  const void* scale);

bool g_leniencyHookHealthy = false;
bool g_markerScaleHooksHealthy = false;
SetPosAndScaleFn g_setPosAndScale = nullptr;
std::atomic<uintptr_t> g_activeMarkerOwner{0};
float g_markerPlayerPos[3]{};
bool g_markerPlayerValid = false;

u32 floatToBits(float value) {
    u32 bits = 0;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float readFloat(const void* base, ptrdiff_t offset) {
    float value = 0.0f;
    __builtin_memcpy(&value,
                     static_cast<const u8*>(base) + offset,
                     sizeof(value));
    return value;
}

bool plausibleCoordinate(float value) {
    return value == value && value > -100000.0f &&
           value < 100000.0f;
}

} // namespace

void init(uintptr_t mainBase, bool leniencyHookHealthy,
          bool markerScaleHooksHealthy) {
    g_leniencyHookHealthy = leniencyHookHealthy;
    g_markerScaleHooksHealthy = markerScaleHooksHealthy;
    if (markerScaleHooksHealthy) {
        g_setPosAndScale = reinterpret_cast<SetPosAndScaleFn>(
            mainBase + kSetPosAndScaleOffset);
    }
}

u32 validationSpanBits() {
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

    u16 reason = 0;
    __builtin_memcpy(&reason,
                     static_cast<u8*>(manager) + 0x7C,
                     sizeof(reason));
    if (!pure::canRelaxQueryFailure(reason)) return false;

    // Fixed LENIENT policy: clear only the two proven local-shape reasons; every other lane stays native.
    const u16 cleared = static_cast<u16>(
        pure::clearLenientReasons(reason));
    __builtin_memcpy(static_cast<u8*>(manager) + 0x7C,
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
        static_cast<const u8*>(updateContext) + 8,
        sizeof(actor));
    if (!actor) return;

    const float px = readFloat(actor, 0x2B4);
    const float py = readFloat(actor, 0x2B8);
    const float pz = readFloat(actor, 0x2BC);
    if (!plausibleCoordinate(px) ||
        !plausibleCoordinate(py) ||
        !plausibleCoordinate(pz))
        return;

    g_markerPlayerPos[0] = px;
    g_markerPlayerPos[1] = py;
    g_markerPlayerPos[2] = pz;
    g_markerPlayerValid = true;
    g_activeMarkerOwner.store(
        reinterpret_cast<uintptr_t>(manager),
        std::memory_order_release);
}

void endMarkerPostCalc(void* manager) {
    uintptr_t expected = reinterpret_cast<uintptr_t>(manager);
    g_activeMarkerOwner.compare_exchange_strong(
        expected, 0, std::memory_order_acq_rel);
    g_markerPlayerValid = false;
}

void applyMarkerScale(void* handle, const void* position) {
    if (!handle || !position || !g_setPosAndScale ||
        !g_markerPlayerValid)
        return;

    const uintptr_t owner =
        g_activeMarkerOwner.load(std::memory_order_acquire);
    if (!owner) return;

    // Limit scaling to CeilingClipper's four native success/failure
    // ring/grid handles while its own postCalc is drawing them.
    const uintptr_t candidate = reinterpret_cast<uintptr_t>(handle);
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

    const float distance =
        __builtin_sqrtf(dx * dx + dy * dy + dz * dz);
    const float scale = pure::markerScale(distance);
    const float uniformScale[3]{scale, scale, scale};
    g_setPosAndScale(handle, position, uniformScale);
}

} // namespace zonai_ascend
