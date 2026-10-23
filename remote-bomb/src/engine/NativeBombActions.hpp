// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>

namespace remote_bomb::engine {
struct ActorLink {
    std::uintptr_t vtable{}, reference{};
    std::int32_t id = -1;
    std::uint8_t enabled = 1, disconnected{}, avoidDelete{}, padding{};
};
static_assert(sizeof(ActorLink) == 24);

struct Position { float x{}, y{}, z{}; };
struct Actor { void *value{}; std::uint32_t scene{}; };
struct NativeReference {
    void *value{};
    std::uint8_t counted{}, padding[7]{};
    ~NativeReference() {} // Nontrivial native return uses X8; releaseReference owns cleanup.
};
static_assert(sizeof(NativeReference) == 16 && offsetof(NativeReference, counted) == 8);
enum class Status { Accepted, StaleActor, MissingBindings, MissingChemical, DeathRefused };
enum class DetonationRoute { Chemical, DeathDrop };

struct Bindings {
    void *(*getLifter)(void *){};
    void *(*getChemical)(void *){};
    void (*detonate)(void *, unsigned, int, const Position *){};
    bool (*isExploded)(void *, int){};
    NativeReference (*heldReference)(void *){};
    void (*releaseReference)(NativeReference *){};
    bool (*deleteActor)(void *, unsigned){};
    bool (*requestDeath)(void *, unsigned, unsigned){};
};

enum class BindingError { None, NullImage, SignatureMismatch };
struct BindingFailure {
    BindingError error{};
    std::uintptr_t offset{};
    std::uint64_t expected{}, actual{};
};

Status requestDetonation(const Bindings &native, std::uint32_t scene, Actor bomb,
                         DetonationRoute route = DetonationRoute::Chemical);
bool explosionReported(const Bindings &native, std::uint32_t scene, Actor bomb);
Actor carriedActor(const Bindings &native, std::uint32_t scene, Actor player);
bool discard(const Bindings &native, std::uint32_t scene, Actor actor);
bool bindTotk121(std::uintptr_t mainBase, Bindings &out, BindingFailure *failure = nullptr);
}
