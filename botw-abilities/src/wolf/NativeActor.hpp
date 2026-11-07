// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/core/Units.hpp"
#include <cstddef>
#include <cstdint>

namespace botw_abilities::wolf {

bool initializeActorApi(std::uintptr_t base);
[[nodiscard]] bool isWolfActor(std::uintptr_t address);

// Nontrivial destruction selects the native X8 return ABI and releases the borrowed actor.
class ActorReference {
public:
    ActorReference() = default;
    ActorReference(const ActorReference&) = delete;
    ActorReference& operator=(const ActorReference&) = delete;
    ~ActorReference();
    [[nodiscard]] std::uintptr_t address() const { return actor_; }
    [[nodiscard]] std::uint32_t generation() const;
    // Only accept an actor whose current native callback already guarantees its lifetime.
    [[nodiscard]] static ActorReference fromLive(std::uintptr_t actor) { return ActorReference(actor); }
private:
    explicit ActorReference(std::uintptr_t actor);
    std::uintptr_t actor_{};
    std::uint8_t counted_{};
    std::uint8_t padding_[7]{};
};
static_assert(sizeof(ActorReference) == 0x10);

enum class ActorStatus : std::uint8_t { Empty, Creating, Ready, Retiring, Failed };

// Keep this owner at a stable address until poll() reports Empty or Failed.
class NativeActor {
public:
    NativeActor() = default;
    NativeActor(const NativeActor&) = delete;
    NativeActor& operator=(const NativeActor&) = delete;
    [[nodiscard]] bool create(const totk::core::WorldTransform& transform);
    [[nodiscard]] ActorStatus poll();
    void retire() { retiring_ = true; }
    [[nodiscard]] ActorReference borrow();
    [[nodiscard]] std::uint32_t generation() const { return link_.id; }
private:
    struct Link {
        std::uintptr_t unusedVtable{}, data{};
        std::uint32_t id = UINT32_MAX;
        std::uint8_t flags[4]{};
    };
    static_assert(sizeof(Link) == 0x18 && offsetof(Link, id) == 0x10);
    std::uintptr_t request_{};
    Link link_{};
    bool retiring_{}, deleteRequested_{}, deleteRefusalReported_{};
};
static_assert(sizeof(NativeActor) == 40);
}
