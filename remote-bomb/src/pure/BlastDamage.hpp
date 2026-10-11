// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombPressure.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace remote_bomb::blast {
inline constexpr float kDamageStartRadius = .001f, kDamageMaxRadius = 6.f;
inline constexpr float kDamageGrowth = 2.f, kDamageFrames = 5.f;
inline constexpr float kDamageTimerEpsilon = 0x1p-23f;
enum class DamageStep { Advanced, Paused, Expired, Invalid };
struct DamageSensor {
    float radius{kDamageStartRadius}, remaining{kDamageFrames};
    bool active{true};

    DamageStep advance(float frames) {
        if (!std::isfinite(frames) || frames < 0.f)
            return DamageStep::Invalid;
        if (!active)
            return DamageStep::Expired;
        if (frames == 0.f)
            return DamageStep::Paused;
        if (remaining <= kDamageTimerEpsilon) {
            active = false;
            return DamageStep::Expired;
        }
        remaining -= frames;
        const auto next = radius + kDamageGrowth * frames;
        radius = next >= kDamageMaxRadius || next < radius ? kDamageMaxRadius : next;
        return DamageStep::Advanced;
    }
};
enum class DamageSync { Started, Advanced, Unchanged, Expired, Invalid, Rewound, IdentityChanged, Capacity };
template <unsigned Capacity = 8>
struct DamageSources {
    struct Source {
        std::uintptr_t element{}, object{}, actor{};
        Vector origin{};
        float nativeAge{};
        DamageSensor sensor;
    };
    std::array<Source, Capacity> entries{};

    std::optional<Source> remove(std::uintptr_t element) {
        for (auto &source : entries)
            if (source.element && source.element == element) {
                const auto removed = source;
                source = {};
                return removed;
            }
        return {};
    }
    std::optional<Source> find(std::uintptr_t object, std::uintptr_t actor) const {
        for (const auto &source : entries)
            if (source.element && source.object == object && source.actor == actor)
                return source;
        return {};
    }
    DamageSync synchronize(std::uintptr_t element, std::uintptr_t object, std::uintptr_t actor,
                           Vector origin, float nativeAge, float frameDelta) {
        if (!element || !object || !actor || !std::isfinite(nativeAge) || nativeAge < 0.f ||
            !std::isfinite(frameDelta) || frameDelta < 0.f ||
            !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
            return DamageSync::Invalid;
        Source *slot{};
        for (auto &source : entries)
            if (source.element == element) {
                slot = &source;
                break;
            }
        bool started{};
        if (!slot) {
            for (auto &source : entries)
                if (!source.element) {
                    source = {element, object, actor, origin, 0.f, {}};
                    slot = &source;
                    started = true;
                    break;
                }
        }
        if (!slot)
            return DamageSync::Capacity;
        if (slot->object != object || slot->actor != actor)
            return DamageSync::IdentityChanged;
        if (nativeAge < slot->nativeAge) {
            slot->sensor.active = false;
            return DamageSync::Rewound;
        }
        slot->origin = origin;
        const auto result = slot->sensor.advance(nativeAge == slot->nativeAge ? 0.f : frameDelta);
        slot->nativeAge = nativeAge;
        if (started)
            return DamageSync::Started;
        if (result == DamageStep::Expired)
            return DamageSync::Expired;
        return result == DamageStep::Advanced ? DamageSync::Advanced : DamageSync::Unchanged;
    }
};
}
