// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "StasisTarget.hpp"
#include "totk/engine/Pointer.hpp"
#include <cstring>
namespace stasis::target {
namespace eng = totk::engine;
bool Mailbox::begin(Ray ray, Vec3 nearPlayer, std::uint32_t generation, Group exclude) {
    if (!ray.valid || !finite(ray.eye) || !finite(ray.direction) || !finite(nearPlayer) ||
        !exclude.enabled)
        return false;
    unsigned idle = Idle;
    if (!state_.compare_exchange_strong(idle, Working, std::memory_order_acq_rel))
        return false;
    ray_ = ray;
    nearPlayer_ = nearPlayer;
    generation_ = generation;
    hit_ = {};
    exclude_ = exclude;
    cancelled_.store(false, std::memory_order_release);
    state_.store(Pending, std::memory_order_release);
    return true;
}
bool Mailbox::poll(Hit &hit, std::uint32_t generation) {
    if (state_.load(std::memory_order_acquire) != Ready)
        return false;
    hit = generation == generation_ ? hit_ : Hit{};
    state_.store(Idle, std::memory_order_release);
    return true;
}
void Mailbox::cancel() {
    cancelled_.store(true, std::memory_order_release);
    auto state = state_.load(std::memory_order_acquire);
    while (state == Pending || state == Ready)
        if (state_.compare_exchange_weak(state, Idle, std::memory_order_acq_rel))
            return;
}
bool Mailbox::busy() const { return state_.load(std::memory_order_acquire) != Idle; }
void Mailbox::observe(Cast original, const void *from, const void *object, Resolve resolve) {
    if (!original || !from || !object)
        return;
    unsigned pending = Pending;
    if (!state_.compare_exchange_strong(pending, Working, std::memory_order_acq_rel))
        return;
    Vec3 donor;
    std::memcpy(&donor, from, sizeof(donor));
    const auto fromPlayer = length(sub(donor, nearPlayer_));
    if (!std::isfinite(fromPlayer) || fromPlayer > 8.0f) {
        state_.store(cancelled_.load(std::memory_order_acquire) ? Idle : Pending,
                     std::memory_order_release);
        return;
    }
    std::memcpy(query_, object, sizeof(query_));
    query_[0x20] = 0;
    // The group argument rewrites the filter at query+0x60; accept only the known inline layout and point the copy at its own filter so those writes stay off the donor.
    const auto source =
        eng::readMemory<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(query_) + 0x60);
    hit_ = {};
    if (source != reinterpret_cast<std::uintptr_t>(object) + 0x128) {
        state_.store(cancelled_.load(std::memory_order_acquire) ? Idle : Ready,
                     std::memory_order_release);
        return;
    }
    eng::writeMemory<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(query_) + 0x60,
                                     reinterpret_cast<std::uintptr_t>(query_) + 0x128);
    hit_.filterValid = true;
    eng::writeMemory<std::uint64_t>(reinterpret_cast<std::uintptr_t>(query_) + 0x120, ~0ull);
    eng::writeMemory<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(query_) + 0x170, 0);
    const Vec3 end = add(ray_.eye, scale(ray_.direction, kReach));
    original(&ray_.eye, &end, query_, &exclude_, kSelectionLayerMaskIndex, 0);
    hit_.hit = query_[0x20] != 0;
    std::memcpy(&hit_.position, query_ + 0x24, sizeof(Vec3));
    const auto body =
        eng::readMemory<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(query_) + 0x170);
    if (hit_.hit && eng::isPlausibleAddress(body) && resolve)
        resolve(body, hit_);
    state_.store(cancelled_.load(std::memory_order_acquire) ? Idle : Ready,
                 std::memory_order_release);
}
} // namespace stasis::target
