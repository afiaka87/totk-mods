// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "AimRaycaster.hpp"

#include <lib.hpp>

#include <atomic>
#include <cmath>

namespace zonai_hookshot::aim {
namespace {
using namespace zonai_hookshot::pure;

namespace off {
constexpr std::ptrdiff_t kGetMotionType = 0x006AB438;  // phive rigid-body class
}  // namespace off

namespace ray {
constexpr std::ptrdiff_t kHit = 0x20;         // BYTE
constexpr std::ptrdiff_t kPosition = 0x24;
constexpr std::ptrdiff_t kNormal = 0x30;
constexpr std::ptrdiff_t kShapeFlags = 0x50;  // resolved tag, bit 0 NoClimb
constexpr std::ptrdiff_t kBodySdkId = 0x120;  // hit body id (u64, -1 = none)
constexpr std::ptrdiff_t kHitBody = 0x170;    // resolved hit rigid-body object
}  // namespace ray

enum class RayState : std::uint32_t { Idle, Pending, Working, Ready };

bool okPtr(std::uintptr_t pointer) {
    return pointer >= 0x1000 && (pointer & 7) == 0 && pointer < (1ull << 40);
}

// Everything below rayState is published before the Pending store and read by the worker only
// after it wins the Pending->Working exchange.
struct Mailbox {
    std::uintptr_t base = 0;
    std::atomic<std::uint32_t> rayState{static_cast<std::uint32_t>(RayState::Idle)};
    Vec3 from{};
    Vec3 to{};
    float near[3]{};
    std::uint32_t mask = kSolidMask;
    std::uint64_t result = 0;
    alignas(16) unsigned char object[0x200]{};
    engine::RayGroup excludedGroup{};
    bool exclude = false;
    std::uint64_t startedTick = 0;
    RequestId id{};
    Vec3 direction{};
    // Classifier staging, written by the worker while the hit body is alive.
    bool bodyKnown = false;
    std::uint32_t motion = 0;
};

Mailbox g_mailbox{};

}  // namespace

void initialize(std::uintptr_t mainBase) { g_mailbox.base = mainBase; }

bool request(const Vec3& from, const Vec3& to, const Vec3& nearPoint,
             const Vec3& direction, std::uint32_t& issueSequence,
             std::uint32_t generation, std::uint64_t tick, const engine::RayGroup* exclude) {
    (void)direction;
    const Vec3 delta = sub(to, from);
    const float span = length(delta);
    if (!finite3(from) || !finite3(to) || !finite3(nearPoint) ||
        !std::isfinite(span) || span <= 0) return false;
    std::uint32_t expected = static_cast<std::uint32_t>(RayState::Idle);
    if (!g_mailbox.rayState.compare_exchange_strong(
            expected, static_cast<std::uint32_t>(RayState::Working),
            std::memory_order_acq_rel)) {
        return false;
    }
    g_mailbox.from = from;
    g_mailbox.to = to;
    g_mailbox.near[0] = nearPoint.x;
    g_mailbox.near[1] = nearPoint.y;
    g_mailbox.near[2] = nearPoint.z;
    g_mailbox.mask = kSolidMask;
    g_mailbox.direction = mul(delta, 1.0f / span);
    g_mailbox.id = RequestId{++issueSequence, generation};
    g_mailbox.startedTick = tick;
    g_mailbox.exclude = exclude != nullptr;
    g_mailbox.excludedGroup = exclude ? *exclude : engine::RayGroup{};
    // Publish the request last.
    g_mailbox.rayState.store(static_cast<std::uint32_t>(RayState::Pending),
                             std::memory_order_release);
    return true;
}

Poll service(std::uint64_t tick, TargetSample& out) {
    const std::uint32_t state =
        g_mailbox.rayState.load(std::memory_order_acquire);
    if (state == static_cast<std::uint32_t>(RayState::Pending)) {
        if (tick - g_mailbox.startedTick > static_cast<std::uint64_t>(kTimeoutTicks)) {
            std::uint32_t expected = static_cast<std::uint32_t>(RayState::Pending);
            if (g_mailbox.rayState.compare_exchange_strong(
                    expected, static_cast<std::uint32_t>(RayState::Idle),
                    std::memory_order_acq_rel)) {
                return Poll::TimedOut;
            }
        }
        return Poll::Quiet;
    }
    if (state != static_cast<std::uint32_t>(RayState::Ready)) return Poll::Quiet;

    TargetSample sample{};
    sample.completed = true;
    sample.hit = *(const unsigned char*)(g_mailbox.object + ray::kHit) != 0;
    sample.position = {*(float*)(g_mailbox.object + ray::kPosition + 0),
                       *(float*)(g_mailbox.object + ray::kPosition + 4),
                       *(float*)(g_mailbox.object + ray::kPosition + 8)};
    sample.normal = {*(float*)(g_mailbox.object + ray::kNormal + 0),
                     *(float*)(g_mailbox.object + ray::kNormal + 4),
                     *(float*)(g_mailbox.object + ray::kNormal + 8)};
    sample.shapeFlags = *(std::uint32_t*)(g_mailbox.object + ray::kShapeFlags);
    sample.rayDirection = g_mailbox.direction;
    sample.generation = g_mailbox.id.generation;
    sample.sequence = g_mailbox.id.sequence;
    sample.hitBodyKnown = g_mailbox.bodyKnown;
    sample.hitMotionType = g_mailbox.motion;
    g_mailbox.rayState.store(static_cast<std::uint32_t>(RayState::Idle),
                             std::memory_order_release);
    out = sample;
    return Poll::Ready;
}

void abandonPending() {
    std::uint32_t expected = static_cast<std::uint32_t>(RayState::Pending);
    g_mailbox.rayState.compare_exchange_strong(
        expected, static_cast<std::uint32_t>(RayState::Idle),
        std::memory_order_acq_rel);
}

// Runs in the raycast worker on the physics thread: piggybacks on a game-issued near-player cast, then re-runs our
// ray through the copied object; results are cleared first so a missing body pointer reads as unknown.
void observe(wwpg::RaycastFn original, const void* from, const void* object) {
    if (!original || !from || !object) return;
    std::uint32_t expected = static_cast<std::uint32_t>(RayState::Pending);
    if (!g_mailbox.rayState.compare_exchange_strong(
            expected, static_cast<std::uint32_t>(RayState::Working),
            std::memory_order_acq_rel)) {
        return;
    }
    // Near-player filter against the position captured by value; the worker must never dereference
    // an actor pointer.
    const float* castFrom = static_cast<const float*>(from);
    const float dx = castFrom[0] - g_mailbox.near[0];
    const float dy = castFrom[1] - g_mailbox.near[1];
    const float dz = castFrom[2] - g_mailbox.near[2];
    if (dx * dx + dy * dy + dz * dz >= 64.0f) {
        g_mailbox.rayState.store(static_cast<std::uint32_t>(RayState::Pending),
                                 std::memory_order_release);
        return;
    }
    for (int i = 0; i < 0x200; ++i)
        g_mailbox.object[i] = static_cast<const unsigned char*>(object)[i];
    *(unsigned char*)(g_mailbox.object + ray::kHit) = 0;
    *(std::uint32_t*)(g_mailbox.object + ray::kShapeFlags) = 0;
    *(std::uint64_t*)(g_mailbox.object + ray::kBodySdkId) = ~0ull;
    *(std::uint64_t*)(g_mailbox.object + ray::kHitBody) = 0;
    if (g_mailbox.exclude && !engine::rebaseRayFilter(g_mailbox.object, object)) {
        Logging.Log("[zonai-hookshot] RAY_FILTER unavailable; refusing request");
    } else {
        g_mailbox.result = original(&g_mailbox.from, &g_mailbox.to, g_mailbox.object,
            g_mailbox.exclude ? &g_mailbox.excludedGroup : nullptr, g_mailbox.mask, 0u);
    }
    g_mailbox.bodyKnown = false;
    g_mailbox.motion = 0;
    if (*(const unsigned char*)(g_mailbox.object + ray::kHit) != 0) {
        // Classify the hit body now, while it is alive; only plain values leave this scope.
        const std::uintptr_t hitBody =
            *(std::uintptr_t*)(g_mailbox.object + ray::kHitBody);
        if (okPtr(hitBody)) {
            const auto getMotionType =
                reinterpret_cast<std::uint32_t (*)(std::uintptr_t)>(
                    g_mailbox.base + off::kGetMotionType);
            g_mailbox.bodyKnown = true;
            g_mailbox.motion = getMotionType(hitBody);
        }
    }
    g_mailbox.rayState.store(static_cast<std::uint32_t>(RayState::Ready),
                             std::memory_order_release);
}

}  // namespace zonai_hookshot::aim
