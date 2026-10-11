// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisSelection.hpp"
#include <atomic>
#include <cstdint>
namespace cryonis::target {
// TotK 1.2.1 PhiveConfig: BowSearchTarget includes characters, objects and ground.
inline constexpr std::uint32_t kSelectionLayerMaskIndex = 44;
inline constexpr std::uint32_t kGroundLayerMaskIndex = 32;
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
    bool hit{}, filterValid{}, clearance{};
};
using Resolve = void (*)(std::uintptr_t, Hit &);
// Native worker queries publish identities that gameplay checks against the fresh roster.
class Mailbox {
  public:
    bool begin(Ray ray, Vec3 nearPlayer, std::uint32_t generation, Group exclude, Target volume);
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
    Target volume_{};
    Group exclude_{};
    std::uint32_t generation_{};
    alignas(16) unsigned char query_[0x200]{};
};
}
