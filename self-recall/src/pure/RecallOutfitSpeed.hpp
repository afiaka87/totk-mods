#pragma once
#include "RecallPlaybackSpeed.hpp"

namespace self_recall::pure {

struct OutfitSpeedProfile {
    const char* label;
    std::array<const char*, 3> seriesBySlot;
    std::array<PlaybackRate, 4> ratesByCount;
};

inline constexpr OutfitSpeedProfile kRecallOutfitSpeed{
    "Glide set", {"Diving", "Diving", "Diving"},
    {PlaybackRate::X125, PlaybackRate::X150, PlaybackRate::X200, PlaybackRate::X400}};

struct OutfitSnapshot {
    std::uint8_t matchedSlots = 0;
    std::array<std::uint32_t, 3> actorIds{};
};

class OutfitSpeed {
    const OutfitSpeedProfile& profile_;
    OutfitSnapshot outfit_{};
    bool initialized_ = false;
public:
    explicit OutfitSpeed(const OutfitSpeedProfile& profile = kRecallOutfitSpeed) : profile_(profile) {}
    unsigned count() const {
        unsigned result = 0;
        for (unsigned slot = 0; slot < 3; ++slot)
            if (outfit_.matchedSlots & (1u << slot)) ++result;
        return result;
    }
    PlaybackRate rate() const { return profile_.ratesByCount[count()]; }
    std::uint8_t mask() const { return outfit_.matchedSlots; }
    bool update(OutfitSnapshot next) {
        next.matchedSlots &= 7;
        for (unsigned slot = 0; slot < 3; ++slot) {
            if (!profile_.seriesBySlot[slot]) next.matchedSlots &= ~(1u << slot);
            if (!(next.matchedSlots & (1u << slot))) next.actorIds[slot] = 0;
        }
        const bool changed = !initialized_ || next.matchedSlots != outfit_.matchedSlots ||
            next.actorIds != outfit_.actorIds;
        outfit_ = next;
        initialized_ = true;
        return changed;
    }
    void reset() { outfit_ = {}; initialized_ = false; }
};
} // namespace self_recall::pure
