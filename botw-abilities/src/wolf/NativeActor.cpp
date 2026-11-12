// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "NativeActor.hpp"
#include "NativeApi.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
using namespace totk::port::literals;
NativeApi g_api;
constexpr const char* kActor = "BotwWolfLink";
constexpr float kActorScale = 1.2f;
constexpr totk::port::Site kSites[]{
    {0xAB92CC, 0xA9BA7BFD, 0xA9016FFC}, {0x83AA68, 0xD10143FF, 0xA9017BFD},
    {0xAB91A8, 0xD10143FF, 0xA9037BFD}, {0xEC9C2C, 0xA9BD7BFD, 0xF9000BF5},
    {0x13C358C, 0xA9BD7BFD, 0xF9000BF5}, {0x86A288, 0xA9BD7BFD, 0xA90157F6},
    {0x9DE880, 0xA9BC7BFD, 0xF9000BF7}, {0xFC128C, 0xF9400008, 0xB5000068},
    {0x1116550, 0xF9400008, 0xB5000068}, {0x158B5B8, 0xF9400008, 0xB5000068},
};


struct CreateArgument {
    float position[3], rotation[9], scale[3];
    const char* name;
    std::uint8_t options[0x10];
    void *blackboard, *opaque60, *parent, *dependent, *creator, *rtti, *heap;
    std::uint8_t flags, padding[7];
};
static_assert(sizeof(CreateArgument) == 0x98 && offsetof(CreateArgument, flags) == 0x90);
static_assert(offsetof(CreateArgument, name) == 0x40 && offsetof(CreateArgument, heap) == 0x88);
}

bool initializeActorApi(std::uintptr_t base) { return g_api.bind(base, kSites, "ACTOR_ABI_REFUSED"); }

bool isWolfActor(std::uintptr_t address) {
    if (!isPlausibleAddress(address)) return false;
    const auto name = readMemory<const char*>(address + layout::kActorNamePointer);
    return name && std::strcmp(name, kActor) == 0;
}

ActorReference::~ActorReference() {
    if (actor_) g_api.at<void(*)(ActorReference*, std::uintptr_t)>(0x86A288)(this, 0);
}

ActorReference::ActorReference(std::uintptr_t actor) {
    if (g_api.ready() && actor) g_api.at<bool(*)(ActorReference*, std::uintptr_t)>(0x86A288)(this, actor);
}

std::uint32_t ActorReference::generation() const {
    return actor_ ? readMemory<std::uint32_t>(actor_ + 0x10) : UINT32_MAX;
}

ActorReference NativeActor::borrow() {
    if (!g_api.ready() || link_.id == UINT32_MAX) return {};
    return g_api.at<ActorReference(*)(Link*)>(0x13C358C)(&link_);
}

bool NativeActor::create(const totk::core::WorldTransform& transform) {
    if (!g_api.ready() || request_ || link_.id != UINT32_MAX || retiring_) {
        Logging.Log("[wolf-link] SPAWN_REFUSED api=%u pending=%u linked=%u retiring=%u",
            unsigned(g_api.ready()), unsigned(request_ != 0), unsigned(link_.id != UINT32_MAX), unsigned(retiring_));
        return false;
    }
    CreateArgument argument{};
    std::memcpy(argument.position, &transform.position, sizeof(argument.position));
    std::memcpy(argument.rotation, transform.rotation.values, sizeof(argument.rotation));
    const auto finite = [](float value) { return std::isfinite(value); };
    if (!std::all_of(std::begin(argument.position), std::end(argument.position), finite) ||
        !std::all_of(std::begin(argument.rotation), std::end(argument.rotation), finite)) {
        Logging.Log("[wolf-link] SPAWN_REFUSED transform=nonfinite");
        return false;
    }
    const auto manager = readMemory<std::uintptr_t>(g_api.base() + 0x4722920_v121);
    if (!isPlausibleAddress(manager)) {
        Logging.Log("[wolf-link] SPAWN_REFUSED manager=%p", reinterpret_cast<void*>(manager));
        return false;
    }
    std::fill(std::begin(argument.scale), std::end(argument.scale), kActorScale);
    argument.name = "";
    argument.flags = 7;
    std::uint32_t result{};
    using Create = bool(*)(std::uintptr_t, const char* const*, const CreateArgument*, std::uintptr_t*, unsigned,
                          void*, void*, void*, bool, std::uint32_t*, void**);
    const bool accepted = g_api.at<Create>(0xAB92CC)(manager, &kActor, &argument, &request_, 1,
        nullptr, nullptr, nullptr, false, &result, nullptr);
    Logging.Log("[wolf-link] SPAWN accepted=%u result=%u pending=%u",
        unsigned(accepted), result, unsigned(request_ != 0));
    return accepted;
}

ActorStatus NativeActor::poll() {
    if (!g_api.ready()) return ActorStatus::Empty;
    if (request_) {
        if (retiring_) {
            if (!g_api.at<bool(*)(std::uintptr_t*, bool)>(0xAB91A8)(&request_, true))
                return ActorStatus::Retiring;
        } else if (g_api.at<bool(*)(std::uintptr_t*)>(0xFC128C)(&request_)) {
            const bool acquired = g_api.at<bool(*)(std::uintptr_t*, Link*, bool, std::uint8_t)>(0xEC9C2C)(
                &request_, &link_, true, 0);
            Logging.Log("[wolf-link] SPAWN_COMPLETE acquired=%u id=%u", unsigned(acquired), link_.id);
        } else if (g_api.at<bool(*)(std::uintptr_t*)>(0x1116550)(&request_) ||
                   g_api.at<bool(*)(std::uintptr_t*)>(0x158B5B8)(&request_)) {
            if (!g_api.at<bool(*)(std::uintptr_t*, bool)>(0xAB91A8)(&request_, false))
                return ActorStatus::Creating;
            Logging.Log("[wolf-link] SPAWN_FAILED native=failed-or-cancelled");
            return ActorStatus::Failed;
        } else {
            return ActorStatus::Creating;
        }
    }
    const auto actor = borrow();
    if (!actor.address()) {
        if (link_.id != UINT32_MAX) g_api.at<void(*)(Link*)>(0x9DE880)(&link_);
        retiring_ = deleteRequested_ = deleteRefusalReported_ = false;
        return ActorStatus::Empty;
    }
    if (!retiring_) return ActorStatus::Ready;
    if (!deleteRequested_) {
        deleteRequested_ = g_api.at<bool(*)(std::uintptr_t, unsigned)>(0x83AA68)(actor.address(), 0);
        if (!deleteRequested_ && !deleteRefusalReported_) {
            Logging.Log("[wolf-link] RETIRE_DEFERRED id=%u", link_.id);
            deleteRefusalReported_ = true;
        }
    }
    return ActorStatus::Retiring;
}
}
