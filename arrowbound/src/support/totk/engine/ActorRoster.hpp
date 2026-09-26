// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include "totk/core/FixedString.hpp"
#include "totk/core/Result.hpp"
#include "totk/core/Units.hpp"
#include "totk/engine/ActorHandle.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Scene.hpp"
#include "totk/engine/Totk121Offsets.hpp"

#include <cstdint>

namespace totk::engine {
enum class VisitControl : std::uint8_t { Continue, Stop };

enum class RosterError : std::uint8_t {
    ManagerUnavailable,
    ListUnavailable,
    CountInvalid,
    ActorNotFound,
    WalkLimitReached,
};

struct ResidentActorView {
    ActorHandle handle{};
    core::ActorName name{};
};

struct RosterWalkReport {
    std::uint32_t visited = 0;
    bool complete = false;
};

template <class Visitor>
[[nodiscard]] core::Result<RosterWalkReport, RosterError>
visitResidentActors(const SceneContext& scene, Visitor&& visitor) {
    if (!scene.isReady()) {
        return core::Result<RosterWalkReport, RosterError>::failure(
            RosterError::ManagerUnavailable);
    }

    const auto count =
        readMemory<std::int32_t>(scene.residentActorManager + layout::kResidentCount);
    const auto list =
        readMemory<std::uintptr_t>(scene.residentActorManager + layout::kResidentList);
    if (!isPlausibleAddress(list)) {
        return core::Result<RosterWalkReport, RosterError>::failure(
            RosterError::ListUnavailable);
    }
    if (count <= 0 || count > 256) {
        return core::Result<RosterWalkReport, RosterError>::failure(
            RosterError::CountInvalid);
    }

    RosterWalkReport report{};
    for (std::int32_t index = 0; index < count; ++index) {
        const auto entry =
            list + static_cast<std::uintptr_t>(index) *
                       static_cast<std::uintptr_t>(layout::kResidentDescriptorStride);
        const auto descriptor =
            readMemory<std::uintptr_t>(entry + layout::kResidentDescriptor);
        if (!isPlausibleAddress(descriptor)) continue;
        const auto actor =
            readMemory<std::uintptr_t>(descriptor + layout::kActorFromDescriptor);
        if (!isPlausibleAddress(actor)) continue;
        const auto namePointer =
            readMemory<std::uintptr_t>(actor + layout::kActorNamePointer);
        if (!isPlausibleStringAddress(namePointer)) continue;

        ResidentActorView view{};
        view.handle = ActorHandle{actor, namePointer, scene.token};
        view.name.assign(reinterpret_cast<const char*>(namePointer));
        ++report.visited;
        if (visitor(view) == VisitControl::Stop) {
            report.complete = false;
            return core::Result<RosterWalkReport, RosterError>::success(report);
        }
    }
    report.complete = true;
    return core::Result<RosterWalkReport, RosterError>::success(report);
}

[[nodiscard]] inline core::Result<ActorHandle, RosterError>
findResidentActor(const SceneContext& scene, const char* name) {
    ActorHandle found{};
    const auto walk = visitResidentActors(scene, [&](const ResidentActorView& actor) {
        if (!actor.name.equals(name)) return VisitControl::Continue;
        found = actor.handle;
        return VisitControl::Stop;
    });
    if (!walk) return core::Result<ActorHandle, RosterError>::failure(walk.error);
    if (!isPlausibleAddress(found.address)) {
        return core::Result<ActorHandle, RosterError>::failure(RosterError::ActorNotFound);
    }
    return core::Result<ActorHandle, RosterError>::success(found);
}

struct PlayerActors {
    ActorHandle player{};
    ActorHandle camera{};
};

[[nodiscard]] inline core::Result<PlayerActors, RosterError>
resolvePlayerActors(const SceneContext& scene) {
    PlayerActors result{};
    const auto walk = visitResidentActors(scene, [&](const ResidentActorView& actor) {
        if (actor.name.equals("Player")) result.player = actor.handle;
        if (actor.name.equals("PlayerCamera")) result.camera = actor.handle;
        return result.player.address && result.camera.address ? VisitControl::Stop
                                                             : VisitControl::Continue;
    });
    if (!walk) return core::Result<PlayerActors, RosterError>::failure(walk.error);
    if (!result.player.address) {
        return core::Result<PlayerActors, RosterError>::failure(RosterError::ActorNotFound);
    }
    return core::Result<PlayerActors, RosterError>::success(result);
}

} // namespace totk::engine
