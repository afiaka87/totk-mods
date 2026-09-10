
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
    ProcessManagerUnavailable,
    ProcessLinkOffsetInvalid,
    ProcessListCorrupt,
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

struct ActiveProcessView {
    ActorHandle handle{};
    core::ActorName processName{};
    std::uint32_t processState = 0;
};

using ProcessListLockFunction = void (*)(std::uintptr_t);

struct ActiveProcessRosterAccess {
    std::uintptr_t manager = 0;
    ProcessListLockFunction lock = nullptr;
    ProcessListLockFunction unlock = nullptr;
    core::SceneToken scene{};
};

[[nodiscard]] inline core::Result<ActiveProcessRosterAccess, RosterError>
resolveActiveProcessRoster(std::uintptr_t mainBase, core::SceneToken scene) {
    if (!isPlausibleAddress(mainBase)) {
        return core::Result<ActiveProcessRosterAccess, RosterError>::failure(
            RosterError::ProcessManagerUnavailable);
    }
    const auto indirect = readMemory<std::uintptr_t>(
        mainBase + Totk121Offsets::kProcessManagerIndirect.value);
    const auto manager = isPlausibleAddress(indirect)
                             ? readMemory<std::uintptr_t>(indirect)
                             : 0;
    if (!isPlausibleAddress(manager)) {
        return core::Result<ActiveProcessRosterAccess, RosterError>::failure(
            RosterError::ProcessManagerUnavailable);
    }
    return core::Result<ActiveProcessRosterAccess, RosterError>::success(
        ActiveProcessRosterAccess{
            manager,
            reinterpret_cast<ProcessListLockFunction>(
                mainBase + Totk121Offsets::kProcessListLock.value),
            reinterpret_cast<ProcessListLockFunction>(
                mainBase + Totk121Offsets::kProcessListUnlock.value),
            scene,
        });
}

template <class Visitor>
[[nodiscard]] core::Result<RosterWalkReport, RosterError>
visitActiveProcesses(const ActiveProcessRosterAccess& roster, Visitor&& visitor) {
    if (!isPlausibleAddress(roster.manager) || !roster.lock || !roster.unlock) {
        return core::Result<RosterWalkReport, RosterError>::failure(
            RosterError::ProcessManagerUnavailable);
    }

    const auto mutex = roster.manager + layout::kProcessListMutex;
    roster.lock(mutex);

    const auto linkOffset = readMemory<std::int32_t>(
        roster.manager + layout::kProcessActiveLinkOffset);
    if (linkOffset <= 0 || linkOffset >= 4096) {
        roster.unlock(mutex);
        return core::Result<RosterWalkReport, RosterError>::failure(
            RosterError::ProcessLinkOffsetInvalid);
    }

    const auto anchor = roster.manager + layout::kProcessActiveHead - 8;
    const auto start =
        readMemory<std::uintptr_t>(roster.manager + layout::kProcessActiveHead);
    auto node = start;
    RosterWalkReport report{};
    std::uint32_t walked = 0;
    while (isPlausibleAddress(node) && node != anchor && walked < 4096) {
        ++walked;
        const auto process = node - static_cast<std::uintptr_t>(linkOffset);
        if (!isPlausibleAddress(process)) {
            roster.unlock(mutex);
            return core::Result<RosterWalkReport, RosterError>::failure(
                RosterError::ProcessListCorrupt);
        }

        const auto state =
            readMemory<std::uint32_t>(process + layout::kProcessState);
        if (state >= 2 && state <= 6) {
            const auto processName =
                readMemory<std::uintptr_t>(process + layout::kProcessName);
            const auto identityName =
                readMemory<std::uintptr_t>(process + layout::kActorNamePointer);
            if (isPlausibleStringAddress(processName) &&
                isPlausibleStringAddress(identityName)) {
                ActiveProcessView view{};
                view.handle = ActorHandle{process, identityName, roster.scene};
                view.processName.assign(reinterpret_cast<const char*>(processName));
                view.processState = state;
                ++report.visited;
                if (visitor(view) == VisitControl::Stop) {
                    roster.unlock(mutex);
                    return core::Result<RosterWalkReport, RosterError>::success(report);
                }
            }
        }

        node = readMemory<std::uintptr_t>(node + sizeof(std::uintptr_t));
        if (node == start) break;
    }

    report.complete = node == anchor;
    roster.unlock(mutex);
    if (!report.complete && walked >= 4096) {
        return core::Result<RosterWalkReport, RosterError>::failure(
            RosterError::WalkLimitReached);
    }
    if (!report.complete && !isPlausibleAddress(node)) {
        return core::Result<RosterWalkReport, RosterError>::failure(
            RosterError::ProcessListCorrupt);
    }
    return core::Result<RosterWalkReport, RosterError>::success(report);
}

} // namespace totk::engine
