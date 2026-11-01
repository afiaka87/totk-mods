#include "TallSpeed.hpp"

#include <cstddef>
#include <cstring>
#include <atomic>

namespace zonai_ascend::speed {
namespace {
GetPlayer g_getPlayer = nullptr;
std::atomic<void*> g_approachPlayer{nullptr};

template<class T> T read(const void* base, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const std::byte*>(base)+offset, sizeof(value));
    return value;
}

float playerMultiplier(void* player) {
    if (!g_getPlayer || !player) return 1.0f;
    return multiplier(read<remote::Vec3>(player, 0x1344), read<remote::Vec3>(player, 0x132C),
                      read<std::uint32_t>(player, 0xAE4));
}
}

void bind(GetPlayer getPlayer) {
    g_getPlayer = getPlayer;
    g_approachPlayer.store(nullptr, std::memory_order_relaxed);
}

ApproachScope::ApproachScope(void* node) {
    void* player = g_getPlayer && node && read<unsigned>(node, 0x20) == 1 ? g_getPlayer(node) : nullptr;
    previous_ = g_approachPlayer.exchange(player, std::memory_order_relaxed);
}

ApproachScope::~ApproachScope() {
    g_approachPlayer.store(previous_, std::memory_order_relaxed);
}

float scaleForwardStep(void* player, float nativeStep, bool travelCall) {
    if (!player) return nativeStep;
    const auto flags = read<std::uint32_t>(player, 0xAE4);
    const bool approach = (flags & kApproach) != 0;
    if (approach ? g_approachPlayer.load(std::memory_order_relaxed) != player : !travelCall)
        return nativeStep;
    const float factor = playerMultiplier(player);
    return scalePositive(nativeStep, factor);
}

float scaleApproachLookahead(void* node, float nativeStep) {
    if (!g_getPlayer || !node || read<unsigned>(node, 0x20) != 1) return nativeStep;
    void* player = g_getPlayer(node);
    if (!player || !(read<std::uint32_t>(player, 0xAE4) & kApproach)) return nativeStep;
    return scalePositive(nativeStep, playerMultiplier(player));
}

void extendBraking(void* node) {
    if (!g_getPlayer || !node) return;
    void* player = g_getPlayer(node);
    if (!player || !(read<std::uint32_t>(player, 0xAE4) & kTravel) ||
        !remote::near(read<remote::Vec3>(node, 0x3C), read<remote::Vec3>(player, 0x132C), 1.81f))
        return;
    // The caller has just recomputed the native radius. A longer braking region retains its near-exit slope.
    const float radius = scalePositive(read<float>(node, 0x58), playerMultiplier(player));
    std::memcpy(static_cast<std::byte*>(node)+0x58, &radius, sizeof(radius));
}

}
