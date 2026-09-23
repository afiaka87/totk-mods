#pragma once

#include <cstddef>
#include <cstdint>

#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/Scene.hpp"
#include "pure/PairPolicy.hpp"
#include "pure/VehicleMatch.hpp"

namespace linked_stick::engine {

enum class WorldObservation : std::uint8_t {
    Unavailable,
    SameScene,
    ChangedScene,
};

enum class StickScanStatus : std::uint8_t {
    NotAttempted,
    PlayerUnavailable,
    RosterUnavailable,
    WalkFailed,
    Complete,
};

struct StickScanReport {
    StickScanStatus status = StickScanStatus::NotAttempted;
    std::uint32_t visited = 0;
    std::uint32_t matchingSticks = 0;
    std::uint32_t imaginarySticks = 0;

    [[nodiscard]] bool ready() const {
        return status == StickScanStatus::Complete;
    }
};

struct NearbyStick {
    totk::engine::ActorHandle actor{};
    float distanceMeters = -1.0f;
    pure::StickCandidateKind kind = pure::StickCandidateKind::None;
};

struct WorldSnapshot {
    WorldObservation observation = WorldObservation::Unavailable;
    totk::engine::SceneContext scene{};
    totk::engine::ActorHandle player{};
    NearbyStick nearest{};
    StickScanReport scan{};
};

class StickWorld {
public:
    void configure(std::uintptr_t mainBase) { mainBase_ = mainBase; }
    WorldSnapshot refresh();
    pure::VehicleShape vehicleShape(const totk::engine::ActorHandle& stick) const;

    [[nodiscard]] bool current(
        const totk::engine::ActorHandle& actor) const;
    [[nodiscard]] std::uintptr_t receiverComponent(
        const totk::engine::ActorHandle& actor) const;
    [[nodiscard]] std::uintptr_t ridableComponent(
        const totk::engine::ActorHandle& actor) const;
    [[nodiscard]] std::uintptr_t riderSeat(
        std::uintptr_t ridable, std::uint32_t seatIndex) const;

private:
    [[nodiscard]] pure::StickCandidateKind classifyStick(
        const totk::engine::ActiveProcessView& view) const;
    [[nodiscard]] std::uintptr_t component(
        const totk::engine::ActorHandle& actor,
        std::ptrdiff_t registryOffset) const;

    std::uintptr_t mainBase_ = 0;
    totk::engine::SceneTracker sceneTracker_{};
    totk::engine::SceneContext scene_{};
};

}
