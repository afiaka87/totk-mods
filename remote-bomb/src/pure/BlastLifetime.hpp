// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace remote_bomb::blast {
enum class LifetimePhase { AwaitingPreparation, Active, Finished };
enum class LifetimeArm { Started, Existing, IdentityChanged, Invalid, Capacity };

template <unsigned Capacity = 8>
struct Lifetimes {
    struct Source {
        std::uintptr_t element{}, object{}, actor{};
        LifetimePhase phase{LifetimePhase::AwaitingPreparation};
        bool retained() const { return element && phase != LifetimePhase::Finished; }
    };
    std::array<Source, Capacity> entries{};

    LifetimeArm arm(std::uintptr_t element, std::uintptr_t object, std::uintptr_t actor) {
        if (!element || !object || !actor)
            return LifetimeArm::Invalid;
        for (const auto &source : entries)
            if (source.element == element)
                return source.object == object && source.actor == actor ? LifetimeArm::Existing
                                                                       : LifetimeArm::IdentityChanged;
        for (auto &source : entries)
            if (!source.element) {
                source = {element, object, actor};
                return LifetimeArm::Started;
            }
        return LifetimeArm::Capacity;
    }
    bool observe(std::uintptr_t element, bool active) {
        for (auto &source : entries)
            if (element && source.element == element) {
                if (source.phase != LifetimePhase::Finished)
                    source.phase = active ? LifetimePhase::Active : LifetimePhase::Finished;
                return true;
            }
        return false;
    }
    std::optional<Source> findActor(std::uintptr_t actor) const {
        for (const auto &source : entries)
            if (actor && source.actor == actor && source.retained())
                return source;
        return {};
    }
    std::optional<Source> remove(std::uintptr_t element) {
        for (auto &source : entries)
            if (element && source.element == element) {
                const auto removed = source;
                source = {};
                return removed;
            }
        return {};
    }
};
}
