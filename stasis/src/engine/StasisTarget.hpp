// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "StasisCore.hpp"
#include <atomic>
#include <cstdint>
namespace stasis::target {
// TotK 1.2.1 collision layer for bow targeting: characters, objects and ground.
inline constexpr std::uint32_t kSelectionLayerMaskIndex = 44;
inline constexpr float kReach = 30.0f;
using Cast = std::uint64_t (*)(const void *, const void *, const void *, const void *,
                               std::uint32_t, std::uint32_t);
struct Group {
    std::uint32_t id{};
    std::uint8_t enabled{}, padding[3]{};
};
static_assert(sizeof(Group) == 8);
struct Hit {
    std::uintptr_t actor{}, name{};
    Vec3 position{};
    bool hit{}, filterValid{};
};
using Resolve = void (*)(std::uintptr_t, Hit &);
// Runs a copied game raycast on the physics worker; the input hook checks the hit actor
// against a fresh roster before using it.
class Mailbox {
  public:
    bool begin(Ray ray, Vec3 nearPlayer, std::uint32_t generation, Group exclude);
    bool poll(Hit &hit, std::uint32_t generation);
    void cancel();
    bool busy() const;
    void observe(Cast original, const void *from, const void *object, Resolve resolve = nullptr);

  private:
    enum State : unsigned { Idle, Pending, Working, Ready };
    std::atomic<unsigned> state_{Idle};
    std::atomic<bool> cancelled_{};
    Ray ray_{};
    Vec3 nearPlayer_{};
    Hit hit_{};
    Group exclude_{};
    std::uint32_t generation_{};
    alignas(16) unsigned char query_[0x200]{};
};
inline bool sameRay(Ray a, Ray b) {
    return a.valid && b.valid && length(sub(a.eye, b.eye)) < 0.15f &&
           dot(a.direction, b.direction) > 0.9996f;
}
// A completed query can still identify the target after the camera moves. Revalidate
// the current reticle against that actor's current bounds, not the old camera pose.
inline bool pointsAt(Ray ray, Vec3 minimum, Vec3 maximum) {
    if (!ray.valid || !finite(ray.eye) || !finite(ray.direction) ||
        !finite(minimum) || !finite(maximum)) return false;
    const float magnitude = length(ray.direction);
    if (!std::isfinite(magnitude) || magnitude < 0.000001f) return false;
    const auto direction = scale(ray.direction,1.0f/magnitude);
    const float origin[]{ray.eye.x,ray.eye.y,ray.eye.z};
    const float delta[]{direction.x,direction.y,direction.z};
    const float low[]{minimum.x,minimum.y,minimum.z};
    const float high[]{maximum.x,maximum.y,maximum.z};
    float near = 0, far = kReach;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (low[axis] > high[axis]) return false;
        if (std::fabs(delta[axis]) < 0.000001f) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) return false;
            continue;
        }
        const float a = (low[axis]-origin[axis])/delta[axis];
        const float b = (high[axis]-origin[axis])/delta[axis];
        near = std::fmax(near,std::fmin(a,b));
        far = std::fmin(far,std::fmax(a,b));
        if (near > far) return false;
    }
    return true;
}
} // namespace stasis::target
