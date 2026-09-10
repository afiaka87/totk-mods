#pragma once
#include <cstring>
#include "RecallModelView.hpp"
#include "totk/engine/Totk121Offsets.hpp"

namespace self_recall::actor_model {
constexpr std::size_t kModelComponent = 0x10;
constexpr std::size_t kModelFromComponent = 0x28;
constexpr std::size_t kEquipmentUser = 0x230;
constexpr std::size_t kModelController = 0x280;
constexpr std::size_t kPlayerComponent = 0x3A8;
constexpr std::size_t kModelRoot = 0x1F8;
constexpr std::size_t kModelCount = 0x20;
constexpr std::size_t kModelEntries = 0x28;
constexpr std::size_t kActorId = 0x10;

template <class T>
T read(const void* base, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}
inline const void* at(const void* base, std::size_t offset) {
    return static_cast<const std::uint8_t*>(base) + offset;
}
inline const void* registry(const void* actor) {
    return read<const void*>(actor, totk::engine::layout::kActorComponentRegistry);
}
inline const void* actorModel(const void* actor) {
    const auto* components = registry(actor);
    const auto* component = components ? read<const void*>(components, kModelComponent) : nullptr;
    return component ? read<const void*>(component, kModelFromComponent) : nullptr;
}
inline pure::Pose actorPose(const void* actor) {
    pure::Pose pose{};
    std::memcpy(&pose.position, at(actor, totk::engine::layout::kActorPosition), sizeof(pose.position));
    std::memcpy(&pose.rotation, at(actor, totk::engine::layout::kActorRotation), sizeof(pose.rotation));
    return pose;
}

} // namespace self_recall::actor_model
