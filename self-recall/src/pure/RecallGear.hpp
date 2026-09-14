#pragma once

#include "RecallPoseData.hpp"

namespace self_recall::pure {

inline constexpr unsigned kGearStaticSlotFirst = 17;
inline constexpr unsigned kGearParasailSlot = 29;
inline constexpr unsigned kGearFairySlot = 30;
inline constexpr unsigned kGearFuseSlotFirst = 32;
inline constexpr unsigned kGearInvalidSlot = 255;
inline constexpr std::uint64_t kGearIdentityTag = 0xE715000000000000ull;

inline std::uint64_t gearModelToken(std::uint32_t actorId, unsigned model) {
    return kGearIdentityTag | (std::uint64_t{actorId} << 8) | model;
}
inline bool isGearIdentity(const RecordedModelIdentity& id) {
    return (id.unit & 0xffff000000000000ull) == kGearIdentityTag &&
           id.skeleton >= 1 && id.skeleton <= kGearInvalidSlot;
}
inline bool transientGear(unsigned slot) {
    return slot == kGearParasailSlot || slot == kGearFairySlot;
}

struct GearMatch {
    const RecordedModelPose* pose = nullptr;
    bool exact = false;
};

inline GearMatch findRecordedGear(const RecordedPoseFrame& frame, std::uint32_t actorId,
        unsigned slot, unsigned model, std::uint64_t resource, unsigned bones, unsigned materials) {
    GearMatch sameSlot;
    for (unsigned i = frame.header.bodyModelCount; i < frame.header.modelCount; ++i) {
        const auto& pose = frame.models[i];
        const auto& id = pose.identity;
        if (!isGearIdentity(id) || (id.unit & 0xffu) != model) continue;
        const bool exact = id.resource == resource && id.boneCount == bones && id.materialCount == materials;
        if (id.unit == gearModelToken(actorId, model)) return {&pose, exact};
        // A new actor can reuse an unloaded resource's address. Its geometry follows
        // the slot attachment; exact bone replay requires the recorded actor.
        if (id.skeleton == slot + 1 && !sameSlot.pose) sameSlot = {&pose, false};
    }
    return sameSlot;
}

}
