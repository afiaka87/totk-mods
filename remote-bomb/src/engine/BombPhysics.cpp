// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombPhysics.hpp"
#include "totk/engine/CodeCheck.hpp"
#include "BombMotion.hpp"
#include "BombMotionRequest.hpp"
#include "BombIdentity.hpp"
#include "BombVisualBackend.hpp"
#include "totk/engine/Transform.hpp"
#include <lib.hpp>
#include <cstring>

namespace remote_bomb::physics {
namespace eng = totk::engine;
namespace {
std::uintptr_t mainBase{};
constexpr totk::engine::CodeSite guards[]{
    {0xAAD62C, 0xD10143FF, 0xA9017BFD},
    {0xFCE23C, 0xF9408808, 0x9001B30A},
    {0x67E8C8, 0xD101C3FF, 0xA9017BFD},
    {0xEE1920, 0xA9BB7BFD, 0xF9000BF9},
    {0x153F38, 0xD101C3FF, 0xA9027BFD},
    {0x23326FC, 0xA9BC7BFD, 0xF9000BF7},
    {0x656DF4, 0xF9403408, 0xAA0003E1},
};
template <class Function> Function call(std::uintptr_t offset) {
    return reinterpret_cast<Function>(mainBase + offset);
}
std::optional<BombIdentity> owner(std::uintptr_t body) {
    const auto link = call<engine::ActorLink *(*)(std::uintptr_t)>(0xFCE23C)(body);
    if (!link || link->id == -1)
        return std::nullopt;
    const auto actor = call<std::uintptr_t (*)(engine::ActorLink *, void *)>(0x67E8C8)(link, nullptr);
    if (!eng::isPlausibleAddress(actor))
        return std::nullopt;
    const auto name = eng::readMemory<std::uintptr_t>(actor + eng::layout::kActorNamePointer);
    if (!eng::isPlausibleStringAddress(name))
        return std::nullopt;
    totk::core::ActorName copied;
    copied.assign(reinterpret_cast<const char *>(name));
    return identifyBomb(copied.c_str());
}
void refused(std::uintptr_t body, const char *reason, std::uintptr_t value) {
    Logging.Log("[remote-bomb] PHYSICS_REFUSED body=%p reason=%s value=%p",
                reinterpret_cast<void *>(body), reason, reinterpret_cast<void *>(value));
}
void apply(std::uintptr_t body, unsigned requested) {
    const auto identity = owner(body);
    if (!identity || nativeBomb(identity->shape))
        return;
    const auto flags = eng::readMemory<std::uint64_t>(body + 104);
    if ((flags & 0x400000) || (flags & 0x1C) == 4)
        return;
    const auto backendSlot = eng::readMemory<std::uintptr_t>(mainBase + 0x462E038);
    const auto backend = eng::isPlausibleAddress(backendSlot) ? eng::readMemory<std::uintptr_t>(backendSlot) : 0;
    const auto module = eng::isPlausibleAddress(backend) ? eng::readMemory<std::uintptr_t>(backend + 200) : 0;
    const auto manager = eng::isPlausibleAddress(module) ? eng::readMemory<std::uintptr_t>(module + 208) : 0;
    if (!eng::isPlausibleAddress(manager)) {
        refused(body, "manager", manager);
        return;
    }
    const auto count = eng::readMemory<unsigned>(manager + 368);
    const auto presets = eng::readMemory<std::uintptr_t>(manager + 376);
    if (count != 82 || !eng::isPlausibleAddress(presets)) {
        refused(body, "preset-count", count);
        return;
    }
    const auto preset = eng::readMemory<std::uintptr_t>(presets + 8 * motionPreset(requested, flags, count));
    const auto sdk = eng::isPlausibleAddress(preset) ? eng::readMemory<std::uintptr_t>(preset + 72) : 0;
    const auto wrapper = eng::readMemory<std::uintptr_t>(module + 192 + 8 * ((flags >> 5) & 1));
    const auto world = eng::isPlausibleAddress(wrapper) ? eng::readMemory<std::uintptr_t>(wrapper + 224) : 0;
    const auto library = eng::isPlausibleAddress(world) ? eng::readMemory<std::uintptr_t>(world + 2256) : 0;
    if (!eng::isPlausibleAddress(sdk) || !eng::isPlausibleAddress(library)) {
        refused(body, "sdk-or-library", library);
        return;
    }
    const auto sourceId = eng::readMemory<std::uint16_t>(sdk + 8);
    const auto propertyCount = eng::readMemory<unsigned>(library + 72);
    const auto properties = eng::readMemory<std::uintptr_t>(library + 64);
    const auto bodyLink = eng::readMemory<std::uintptr_t>(body + 112);
    if (sourceId >= propertyCount || !eng::isPlausibleAddress(properties) || !eng::isPlausibleAddress(bodyLink)) {
        refused(body, "source-id", sourceId);
        return;
    }
    const auto original = eng::readMemory<MotionProperties>(properties + 112 * sourceId);
    if (original.prefix[0] != 0)
        return;
    auto modified = withBombMotion(original, identity->shape);
    const auto result = call<unsigned (*)(std::uintptr_t, const MotionProperties *)>(0xEE1920)(library, &modified);
    if (result >= 0xFFFF) {
        refused(body, "library-full", result);
        return;
    }
    const auto bodyId = eng::readMemory<std::uint64_t>(bodyLink + 8);
    call<std::uintptr_t (*)(std::uintptr_t, std::uint64_t, std::uint16_t, int)>(0x153F38)(world, bodyId, std::uint16_t(result), 1);
    Logging.Log("[remote-bomb] PHYSICS_MOTION form=%u body=%p preset=%u sdk=%u source=%u damping=%f max_linear=%f max_angular=%f",
                unsigned(identity->shape), reinterpret_cast<void *>(body), requested, result, sourceId,
                double(modified.linearDamping), double(modified.maxLinearVelocity), double(modified.maxAngularVelocity));
}
}
bool initialize(std::uintptr_t base) {
    mainBase = 0;
    if (!base) {
        Logging.Log("[remote-bomb] PHYSICS_BIND_REFUSED base=0");
        return false;
    }
    if (!totk::engine::codeMatches(base, guards, "remote-bomb", "PHYSICS_BIND_REFUSED"))
        return false;
    mainBase = base;
    return true;
}
bool queueRefresh(engine::Handle actor) {
    const auto resolved = eng::TransformService({}).rigidBody(actor, actor.scene);
    if (!resolved) {
        Logging.Log("[remote-bomb] PHYSICS_REFRESH_REFUSED actor=%p error=%u",
                    reinterpret_cast<void *>(actor.address), unsigned(resolved.error));
        return false;
    }
    const auto body = resolved.value.address;
    const RequestBindings native{call<void (*)(std::uintptr_t)>(0x2B17270),
                                 call<void (*)(std::uintptr_t)>(0x2B17280),
                                 call<std::uintptr_t (*)(std::uintptr_t)>(0x656DF4)};
    const auto result = enqueueMotionRefresh(body, native);
    const bool accepted = result.status == RefreshStatus::Queued || result.status == RefreshStatus::AlreadyQueued;
    if (!accepted)
        Logging.Log("[remote-bomb] PHYSICS_REFRESH_REFUSED body=%p status=%u value=%p",
                    reinterpret_cast<void *>(body), unsigned(result.status), reinterpret_cast<void *>(result.value));
    return accepted;
}
void bodyAdded(std::uintptr_t body) {
    apply(body, eng::readMemory<unsigned>(body + 380));
}
void motionChanged(std::uintptr_t body, const int *preset) {
    if (preset)
        apply(body, unsigned(*preset));
}
}
