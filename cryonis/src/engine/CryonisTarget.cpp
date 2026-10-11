// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisTarget.hpp"
#include "totk/engine/Pointer.hpp"
#include <cstring>
namespace cryonis::target {
namespace eng = totk::engine;
bool Mailbox::begin(Ray ray, Vec3 nearPlayer, std::uint32_t generation, Group exclude,
                    Target volume) {
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
    volume_ = volume;
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
    const auto fromPlayer = distance(donor, nearPlayer_);
    if (!std::isfinite(fromPlayer) || fromPlayer > 8.0f) {
        state_.store(cancelled_.load(std::memory_order_acquire) ? Idle : Pending,
                     std::memory_order_release);
        return;
    }
    std::memcpy(query_, object, sizeof(query_));
    query_[0x20] = 0;
    // Point the copy at its own inline filter so native group writes cannot alter the donor.
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
    const Vec3 end = add(ray_.eye, mul(ray_.direction, kReach));
    original(&ray_.eye, &end, query_, &exclude_, kSelectionLayerMaskIndex, 0);
    hit_.hit = query_[0x20] != 0;
    std::memcpy(&hit_.position, query_ + 0x24, sizeof(Vec3));
    const auto body =
        eng::readMemory<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(query_) + 0x170);
    if (hit_.hit && eng::isPlausibleAddress(body) && resolve)
        resolve(body, hit_);
    // Ground-only footprint, ceiling and side probes leave movable props available for lifting.
    hit_.clearance = volume_.valid && finite(volume_.base);
    const auto clear = [&](Vec3 start, Vec3 finish) {
        query_[0x20] = 0;
        eng::writeMemory<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(query_) + 0x170, 0);
        original(&start, &finish, query_, &exclude_, kGroundLayerMaskIndex, 0);
        return query_[0x20] == 0;
    };
    for (unsigned row = 0; row < 3 && hit_.clearance; ++row)
        for (unsigned column = 0; column < 3 && hit_.clearance; ++column) {
            const Vec3 bottom =
                footprintPoint(volume_.base, (float(column) - 1) * (kWidth * .5f - .01f), .075f,
                               (float(row) - 1) * (kWidth * .5f - .01f), volume_.yaw);
            hit_.clearance = clear(bottom, add(bottom, {0, kHeight - kSettledSubmerge, 0}));
        }
    for (unsigned level = 0; level < 3 && hit_.clearance; ++level) {
        const auto centre = add(
            volume_.base, {0, .075f + float(level) * (kHeight - kSettledSubmerge - .15f) * .5f, 0});
        const float half = kWidth * .5f - .01f;
        hit_.clearance = clear(footprintPoint(centre, -half, 0, -half, volume_.yaw),
                               footprintPoint(centre, half, 0, half, volume_.yaw)) &&
                         clear(footprintPoint(centre, -half, 0, half, volume_.yaw),
                               footprintPoint(centre, half, 0, -half, volume_.yaw));
    }
    state_.store(cancelled_.load(std::memory_order_acquire) ? Idle : Ready,
                 std::memory_order_release);
}
}
