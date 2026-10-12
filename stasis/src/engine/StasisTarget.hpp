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
// Runs a copied game raycast on the physics worker; the input hook checks the hit actor against a fresh roster before using it.
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
} // namespace stasis::target
