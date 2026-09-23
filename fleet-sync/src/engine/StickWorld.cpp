#include "engine/StickWorld.hpp"

#include "engine/LinkedStickOffsets.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Totk121Offsets.hpp"

namespace linked_stick::engine {
namespace shared = totk::engine;

namespace {
constexpr float kSelectionRadiusMeters = 8.0f;
constexpr const char* kStickActor = "SpObj_ControlStick_A_01";

float squaredDistance(const float* first, const float* second) {
    const float dx = first[0] - second[0];
    const float dy = first[1] - second[1];
    const float dz = first[2] - second[2];
    return dx * dx + dy * dy + dz * dz;
}
}

pure::StickCandidateKind StickWorld::classifyStick(
    const shared::ActiveProcessView& view) const {
    const bool processNameMatches = view.processName.equals(kStickActor);
    if (processNameMatches)
        return pure::StickCandidateKind::NamedProcess;

    totk::core::ActorName identityName{};
    identityName.assign(reinterpret_cast<const char*>(
        view.handle.capturedNamePointer));
    const bool identityNameMatches = identityName.equals(kStickActor);
    if (identityNameMatches)
        return pure::StickCandidateKind::NamedIdentity;

    const std::uintptr_t registry = shared::readMemory<std::uintptr_t>(
        view.handle.address + offsets::kComponentRegistry);
    const std::uintptr_t receiver =
        shared::isPlausibleAddress(registry)
            ? shared::readMemory<std::uintptr_t>(
                  registry + offsets::kSpecialPowerReceiver)
            : 0;
    const std::uintptr_t ridable =
        shared::isPlausibleAddress(registry)
            ? shared::readMemory<std::uintptr_t>(
                  registry + offsets::kRidable)
            : 0;
    const std::uint64_t receiverFlags =
        shared::isPlausibleAddress(receiver)
            ? shared::readMemory<std::uint64_t>(
                  receiver + offsets::kSpecialPowerReceiverFlags)
            : 0;
    const bool imaginary =
        (receiverFlags & offsets::kImaginaryAutobuildActorFlag) != 0;
    const std::uint32_t seatCount =
        shared::isPlausibleAddress(ridable)
            ? shared::readMemory<std::uint32_t>(
                  ridable + offsets::kRidableSeatCount)
            : 0;
    const std::uintptr_t seats =
        shared::isPlausibleAddress(ridable)
            ? shared::readMemory<std::uintptr_t>(
                  ridable + offsets::kRidableSeatArray)
            : 0;

    return pure::classifyStickCandidate(
        false, false, imaginary,
        shared::isPlausibleAddress(receiver),
        shared::isPlausibleAddress(ridable),
        seatCount > 0 && seatCount <= 16 &&
            shared::isPlausibleAddress(seats));
}

WorldSnapshot StickWorld::refresh() {
    const auto observed =
        sceneTracker_.observe(shared::resolveScene(mainBase_));
    if (observed.observation == shared::SceneObservation::Unavailable) {
        scene_ = {};
        return {};
    }

    scene_ = observed.context;
    WorldSnapshot snapshot{};
    snapshot.observation =
        observed.observation == shared::SceneObservation::SameScene
            ? WorldObservation::SameScene
            : WorldObservation::ChangedScene;
    snapshot.scene = scene_;
    const auto player = shared::findResidentActor(scene_, "Player");
    if (!player) {
        snapshot.scan.status = StickScanStatus::PlayerUnavailable;
        return snapshot;
    }
    snapshot.player = player.value;

    const auto* playerPosition = reinterpret_cast<const float*>(
        snapshot.player.address + shared::layout::kActorPosition);
    float bestSquared =
        kSelectionRadiusMeters * kSelectionRadiusMeters;

    const auto roster =
        shared::resolveActiveProcessRoster(mainBase_, scene_.token);
    if (!roster) {
        snapshot.scan.status = StickScanStatus::RosterUnavailable;
        return snapshot;
    }

    const auto walk = shared::visitActiveProcesses(
        roster.value, [&](const shared::ActiveProcessView& view) {
            ++snapshot.scan.visited;
            const pure::StickCandidateKind kind = classifyStick(view);
            if (kind == pure::StickCandidateKind::None)
                return shared::VisitControl::Continue;
            ++snapshot.scan.matchingSticks;
            if (kind == pure::StickCandidateKind::ImaginaryRidable)
                ++snapshot.scan.imaginarySticks;
            const auto* position = reinterpret_cast<const float*>(
                view.handle.address + shared::layout::kActorPosition);
            const float candidate = squaredDistance(position, playerPosition);
            if (__builtin_isfinite(candidate) && candidate < bestSquared) {
                bestSquared = candidate;
                snapshot.nearest.actor = view.handle;
                snapshot.nearest.kind = kind;
            }
            return shared::VisitControl::Continue;
        });
    if (!walk || !walk.value.complete) {
        snapshot.scan.status = StickScanStatus::WalkFailed;
        snapshot.nearest = {};
        return snapshot;
    }

    snapshot.scan.status = StickScanStatus::Complete;
    if (snapshot.nearest.actor.address)
        snapshot.nearest.distanceMeters = __builtin_sqrtf(bestSquared);
    return snapshot;
}

bool StickWorld::current(const shared::ActorHandle& actor) const {
    return actor.isCurrent(scene_.token);
}

pure::VehicleShape StickWorld::vehicleShape(const shared::ActorHandle& stick) const {
    namespace m = pure::matched;
    pure::VehicleShape result{};
    if (stick.scene != scene_.token) return result;
    const auto roster = shared::resolveActiveProcessRoster(mainBase_, scene_.token);
    if (!roster) return result;
    m::Vec origin{};
    m::Mat inverse{};
    const auto assemblyOf = [&](const shared::ActorHandle& actor) {
        const auto receiver = receiverComponent(actor);
        return receiver ? shared::readMemory<std::uintptr_t>(receiver + offsets::kCombinedActorIntegrator) : 0;
    };
    const auto locate = shared::visitActiveProcesses(roster.value, [&](const shared::ActiveProcessView& view) {
        if (view.handle.address != stick.address ||
            view.handle.capturedNamePointer != stick.capturedNamePointer) return shared::VisitControl::Continue;
        const auto assembly = assemblyOf(view.handle);
        if (!shared::isPlausibleAddress(assembly)) return shared::VisitControl::Continue;
        const auto rotation = shared::readMemory<m::Mat>(stick.address + shared::layout::kActorRotation);
        origin = shared::readMemory<m::Vec>(stick.address + shared::layout::kActorPosition);
        if (!m::validRotation(rotation) || !pure::isFinite(origin)) return shared::VisitControl::Continue;
        inverse = m::transpose(rotation);
        result.assembly = assembly;
        result.expected = shared::readMemory<unsigned>(assembly + offsets::kCombinedActorMemberCount);
        return shared::VisitControl::Continue;
    });
    if (!locate || !locate.value.complete || !result.assembly ||
        result.expected < 2 || result.expected > result.shape.members.size()) return result;
    bool overflow = false, stickSeen = false;
    // Copy geometry under the roster lock; never call physics or retain member pointers here.
    const auto collect = shared::visitActiveProcesses(roster.value, [&](const shared::ActiveProcessView& view) {
        if (assemblyOf(view.handle) != result.assembly) return shared::VisitControl::Continue;
        if (result.shape.count == result.shape.members.size()) {
            overflow = true;
            return shared::VisitControl::Continue;
        }
        stickSeen |= view.handle.address == stick.address &&
                     view.handle.capturedNamePointer == stick.capturedNamePointer &&
                     shared::readMemory<unsigned>(result.assembly + offsets::kCombinedActorMemberCount) == result.expected;
        auto& member = result.shape.members[result.shape.count++];
        member.kind = pure::vehiclePartKind(reinterpret_cast<const char*>(view.handle.capturedNamePointer));
        member.position = m::rotate(inverse, m::sub(shared::readMemory<m::Vec>(
            view.handle.address + shared::layout::kActorPosition), origin));
        member.rotation = m::product(inverse, shared::readMemory<m::Mat>(
            view.handle.address + shared::layout::kActorRotation));
        return shared::VisitControl::Continue;
    });
    result.complete = collect && collect.value.complete && !overflow && stickSeen;
    return result;
}

std::uintptr_t StickWorld::receiverComponent(
    const shared::ActorHandle& actor) const {
    return component(actor, offsets::kSpecialPowerReceiver);
}

std::uintptr_t StickWorld::ridableComponent(
    const shared::ActorHandle& actor) const {
    return component(actor, offsets::kRidable);
}

std::uintptr_t StickWorld::component(
    const shared::ActorHandle& actor,
    std::ptrdiff_t registryOffset) const {
    if (!current(actor)) return 0;
    const std::uintptr_t registry = shared::readMemory<std::uintptr_t>(
        actor.address + offsets::kComponentRegistry);
    if (!shared::isPlausibleAddress(registry)) return 0;
    const std::uintptr_t result = shared::readMemory<std::uintptr_t>(
        registry + registryOffset);
    return shared::isPlausibleAddress(result) ? result : 0;
}

std::uintptr_t StickWorld::riderSeat(std::uintptr_t ridable,
                                     std::uint32_t seatIndex) const {
    if (!shared::isPlausibleAddress(ridable)) return 0;
    const std::uint32_t seatCount = shared::readMemory<std::uint32_t>(
        ridable + offsets::kRidableSeatCount);
    if (seatIndex >= seatCount || seatCount > 16) return 0;
    const std::uintptr_t seats = shared::readMemory<std::uintptr_t>(
        ridable + offsets::kRidableSeatArray);
    if (!shared::isPlausibleAddress(seats)) return 0;
    const std::uintptr_t seat = shared::readMemory<std::uintptr_t>(
        seats + sizeof(std::uintptr_t) * seatIndex);
    return shared::isPlausibleAddress(seat) ? seat : 0;
}

}
