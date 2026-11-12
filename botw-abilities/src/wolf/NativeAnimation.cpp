// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "NativeAnimation.hpp"
#include "NativeApi.hpp"
#include <cmath>
#include <cstring>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
NativeApi g_api;
constexpr totk::port::Site kSites[]{
    {0xFCA854, 0xF9411408, 0xB40000E8}, {0xD280F4, 0xD10643FF, 0x6D1223E9},
    {0xD2881C, 0xD104C3FF, 0xA9117BFD}, {0x834284, 0xD10503FF, 0xA9127BFD},
    {0x22DE96C, 0xD100C3FF, 0xA9017BFD}, {0x2A78844, 0xFC1E0FE8, 0xA900FBFD},
    {0x6C0314, 0x39423008, 0x37181688}, {0x6C9D7C, 0xD104C3FF, 0xA9117BFD},
    {0x10DC754, 0xD104C3FF, 0xA9117BFD}, {0x1E43900, 0x39410408, 0x53041100},
};
constexpr std::uintptr_t kCommandFlags = 172;
constexpr std::uint8_t kSuppressCommands = 8;

std::uintptr_t controller(const ActorReference& actor) {
    const auto as = g_api.ready() && actor.address() ?
        g_api.at<std::uintptr_t(*)(std::uintptr_t)>(0xFCA854)(actor.address()) : 0;
    if (!isPlausibleAddress(as) || readMemory<int>(as + 24) <= 0 ||
        !pointerAt(as, 32) || !pointerAt(as, 64) || !pointerAt(as, 72)) {
        Logging.Log("[wolf-link] ANIMATION_CONTROLLER_MISSING api=%u actor=%p as=%p",
            unsigned(g_api.ready()), reinterpret_cast<void*>(actor.address()), reinterpret_cast<void*>(as));
        return 0;
    }
    return as;
}

bool validName(const char* name) {
    if (name && *name) return true;
    Logging.Log("[wolf-link] ANIMATION_NAME_MISSING name=%p", static_cast<const void*>(name));
    return false;
}

std::uintptr_t quadruped(const ActorReference& actor) {
    const auto quad = g_api.ready() ? actorComponent(actor.address(), 0x3E0) : 0;
    if (!quad) Logging.Log("[wolf-link] ANIMATION_QUADRUPED_MISSING api=%u actor=%p",
        unsigned(g_api.ready()), reinterpret_cast<void*>(actor.address()));
    return quad;
}
}

bool initializeAnimationApi(std::uintptr_t base) { return g_api.bind(base, kSites, "ANIMATION_ABI_REFUSED"); }

std::optional<AnimationState> readAnimation(const ActorReference& actor, const char* command) {
    if (!validName(command)) return std::nullopt;
    const auto as = controller(actor);
    if (!as) return std::nullopt;
    const auto current = g_api.at<const char*(*)(std::uintptr_t, int)>(0xD2881C)(as, 0);
    if (!current) {
        Logging.Log("[wolf-link] ANIMATION_COMMAND_MISSING as=%p", reinterpret_cast<void*>(as));
        return std::nullopt;
    }
    const auto events = g_api.at<std::uintptr_t(*)(std::uintptr_t, int)>(0x10DC754)(as, 0);
    return AnimationState{std::strcmp(current, command) == 0,
        g_api.at<bool(*)(std::uintptr_t, int)>(0x834284)(as, 0),
        isPlausibleAddress(events) && (readMemory<std::uint8_t>(events + 65) & 0x10) != 0};
}

bool playAnimation(const ActorReference& actor, const char* command, float rate, float delta, bool ignoreSame) {
    if (!validName(command)) return false;
    if (!std::isfinite(rate) || rate < 0 || !std::isfinite(delta) || delta < 0) {
        Logging.Log("[wolf-link] ANIMATION_RATE_INVALID rate=%f delta=%f", double(rate), double(delta));
        return false;
    }
    const auto as = controller(actor);
    if (!as) return false;
    if (ignoreSame) {
        const auto current = g_api.at<const char*(*)(std::uintptr_t, int)>(0xD2881C)(as, 0);
        if (current && std::strcmp(current, command) == 0) return true;
    }
    if (!g_api.at<bool(*)(std::uintptr_t, const char**)>(0x22DE96C)(as, &command)) {
        Logging.Log("[wolf-link] ANIMATION_COMMAND_UNKNOWN command=%s", command);
        return false;
    }
    g_api.at<void(*)(std::uintptr_t, int, float)>(0x6C9D7C)(as, 0, rate);
    const bool played = g_api.at<bool(*)(std::uintptr_t, const char**, int, int, bool, bool, bool, float, float)>(
        0xD280F4)(as, &command, 0, 0, false, false, false, delta, -1.0f);
    if (!played) Logging.Log("[wolf-link] ANIMATION_PLAY_REJECTED command=%s as=%p",
        command, reinterpret_cast<void*>(as));
    return played;
}

bool setAnimationFloat(const ActorReference& actor, const char* name, float value) {
    if (!validName(name)) return false;
    if (!std::isfinite(value)) {
        Logging.Log("[wolf-link] ANIMATION_VALUE_INVALID name=%s value=%f", name, double(value));
        return false;
    }
    const auto as = controller(actor);
    if (!as) return false;
    const auto blackboard = readMemory<std::uintptr_t>(as + 64);
    const bool set = g_api.at<bool(*)(std::uintptr_t, const char**, float)>(0x2A78844)(blackboard, &name, value);
    if (!set) Logging.Log("[wolf-link] ANIMATION_PARAMETER_UNKNOWN name=%s", name);
    return set;
}

bool AnimationOverride::owns(const ActorReference& actor) const {
    return owner_ && actor.address() == owner_ && actor.generation() == generation_;
}

bool AnimationOverride::acquire(const ActorReference& actor) {
    if (owner_) {
        const bool same = owns(actor);
        if (!same) Logging.Log("[wolf-link] ANIMATION_OWNER_BUSY old=%p/%u actor=%p/%u",
            reinterpret_cast<void*>(owner_), generation_, reinterpret_cast<void*>(actor.address()), actor.generation());
        return same;
    }
    const auto quad = quadruped(actor);
    if (!quad) return false;
    const auto flags = readMemory<std::uint8_t>(quad + kCommandFlags);
    wasSuppressed_ = (flags & kSuppressCommands) != 0;
    writeMemory(quad + kCommandFlags, std::uint8_t(flags | kSuppressCommands));
    owner_ = actor.address();
    generation_ = actor.generation();
    return true;
}

void AnimationOverride::release(const ActorReference& actor) {
    if (!owner_) return;
    if (owns(actor)) {
        if (const auto quad = quadruped(actor)) {
            const auto flags = readMemory<std::uint8_t>(quad + kCommandFlags);
            writeMemory(quad + kCommandFlags, std::uint8_t(
                (flags & ~kSuppressCommands) | (wasSuppressed_ ? kSuppressCommands : 0)));
        }
    } else {
        Logging.Log("[wolf-link] ANIMATION_OWNER_EXPIRED old=%p/%u actor=%p/%u",
            reinterpret_cast<void*>(owner_), generation_, reinterpret_cast<void*>(actor.address()), actor.generation());
    }
    owner_ = 0;
}

}
