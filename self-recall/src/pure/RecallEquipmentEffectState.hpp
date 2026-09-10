#pragma once
#include "RecallPoseHistory.hpp"
#include <array>
#include <optional>

namespace self_recall::pure {
inline constexpr unsigned kEquipmentEffectLimit = 64;

enum class EquipmentLoopAction { Keep, Kill, Emit, WakeAndEmit };
inline EquipmentLoopAction planEquipmentLoop(bool selected, bool valid, bool sleeping) {
    if (!selected) return valid ? EquipmentLoopAction::Kill : EquipmentLoopAction::Keep;
    if (sleeping) return EquipmentLoopAction::WakeAndEmit;
    return valid ? EquipmentLoopAction::Keep : EquipmentLoopAction::Emit;
}
inline std::uint64_t equipmentEffectMask(const PoseFrameHeader& header) {
    std::uint64_t mask;
    static_assert(sizeof(header.reserved) == sizeof(mask));
    std::memcpy(&mask, header.reserved, sizeof(mask));
    return mask;
}
inline void setEquipmentEffectMask(PoseFrameHeader& header, std::uint64_t mask) {
    std::memcpy(header.reserved, &mask, sizeof(mask));
}
inline bool equipmentEffectSelected(const PoseFrameHeader& header, unsigned index) {
    return index < kEquipmentEffectLimit && (equipmentEffectMask(header) & (std::uint64_t{1} << index));
}

struct EffectMaskIdentity {
    std::uintptr_t executor = 0, emitter = 0;
    std::uint32_t id = 0;
    bool operator==(const EffectMaskIdentity&) const = default;
};
template<unsigned Capacity> class EffectMaskRestoration {
    struct Entry { EffectMaskIdentity identity{}; std::uint32_t mask = 0; };
    std::array<Entry, Capacity> entries_{};
public:
    bool remember(EffectMaskIdentity identity, std::uint32_t mask) {
        if (!identity.executor || !identity.emitter) return false;
        for (const auto& e : entries_) if (e.identity.executor == identity.executor) return false;
        for (auto& e : entries_) if (!e.identity.executor) {
            e = {identity, mask};
            return true;
        }
        return false;
    }
    std::optional<std::uint32_t> restore(EffectMaskIdentity identity) {
        for (auto& e : entries_) if (e.identity.executor == identity.executor && identity.executor) {
            const auto saved = e;
            e = {};
            if (saved.identity == identity) return saved.mask;
            return {}; // Reused executor/emitter: never write an old mask.
        }
        return {};
    }
};
} // namespace self_recall::pure
