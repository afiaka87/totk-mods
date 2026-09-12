
#pragma once

#include "totk/core/Types.hpp"
#include "totk/engine/Runtime.hpp"

#include <cstdint>

namespace totk::engine {

enum class SceneResolveError : std::uint8_t {
    MainImageUnavailable,
    SceneModuleUnavailable,
    SceneUnavailable,
    ComponentsUnavailable,
    ResidentRosterUnavailable,
};

struct SceneContext {
    core::SceneToken token{};
    std::uintptr_t residentActorManager = 0;

    [[nodiscard]] bool isReady() const {
        return token.isValid() && isPlausibleAddress(residentActorManager);
    }
};

using SceneResult = core::Result<SceneContext, SceneResolveError>;

[[nodiscard]] inline SceneResult resolveSceneFromModuleSlot(std::uintptr_t sceneModuleSlot) {
    if (!isPlausibleAddress(sceneModuleSlot)) {
        return SceneResult::failure(SceneResolveError::MainImageUnavailable);
    }

    const auto sceneModule = readMemory<std::uintptr_t>(sceneModuleSlot);
    if (!isPlausibleAddress(sceneModule)) {
        return SceneResult::failure(SceneResolveError::SceneModuleUnavailable);
    }

    const auto scene =
        readMemory<std::uintptr_t>(sceneModule + layout::kSceneFromModule);
    if (!isPlausibleAddress(scene)) {
        return SceneResult::failure(SceneResolveError::SceneUnavailable);
    }

    const auto components =
        readMemory<std::uintptr_t>(scene + layout::kSceneComponents);
    if (!isPlausibleAddress(components)) {
        return SceneResult::failure(SceneResolveError::ComponentsUnavailable);
    }

    const auto manager = readMemory<std::uintptr_t>(
        components + sizeof(std::uintptr_t) * layout::kResidentActorComponentIndex);
    if (!isPlausibleAddress(manager)) {
        return SceneResult::failure(SceneResolveError::ResidentRosterUnavailable);
    }

    return SceneResult::success(SceneContext{core::SceneToken{scene}, manager});
}

[[nodiscard]] inline SceneResult resolveScene(std::uintptr_t mainBase) {
    if (!isPlausibleAddress(mainBase)) {
        return SceneResult::failure(SceneResolveError::MainImageUnavailable);
    }
    return resolveSceneFromModuleSlot(
        mainBase + Totk121Offsets::kSceneModuleInstance.value);
}

enum class VisitControl : std::uint8_t { Continue, Stop };

enum class RosterError : std::uint8_t {
    ManagerUnavailable,
    ListUnavailable,
    CountInvalid,
    ActorNotFound,
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

}
