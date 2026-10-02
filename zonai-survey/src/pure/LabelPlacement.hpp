// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace zonai_survey::atlas {
// A name that would cover a nearer label hides; hysteresis, a return delay and fades stop it blinking.
inline constexpr float kNameBand = 4;               // layout units either side of the touching edge
inline constexpr float kNameReturnSeconds = 0.4f;
inline constexpr float kNameFadeSeconds = 0.2f;
inline constexpr float kNameMatchMeters = 2;
inline constexpr unsigned kMaxLabels = 48;

struct LabelBox { float x0, y0, x1, y1; };
inline bool overlaps(const LabelBox& a, const LabelBox& b) {
    return !(a.x1 < b.x0 || b.x1 < a.x0 || a.y1 < b.y0 || b.y1 < a.y0);
}
inline LabelBox grown(const LabelBox& box, float by) { return {box.x0 - by, box.y0 - by, box.x1 + by, box.y1 + by}; }

// nameRight is the right edge of icon plus name, or 0 for a label without a name.
struct Label {
    std::uint16_t name{};
    float worldX{}, worldZ{}, distanceSq{};
    LabelBox icon{};
    float nameRight{};
    bool hasName() const { return nameRight > icon.x1; }
};

class NamePlacement {
public:
    // Writes each label's name opacity (0..1) and returns how many names switched on or off.
    unsigned place(const Label* labels, unsigned count, float seconds, float* nameAlpha) {
        if (count > kMaxLabels) count = kMaxLabels;
        seconds = seconds < 0 ? 0 : seconds > 0.1f ? 0.1f : seconds;
        Memory next[kMaxLabels]{};
        bool used[kMaxLabels]{};
        for (unsigned i = 0; i < count; ++i) next[i] = recall(labels[i], used);
        unsigned order[kMaxLabels]{};
        placementOrder(labels, next, count, order);
        LabelBox placed[kMaxLabels]{};
        unsigned changes = 0;
        for (unsigned k = 0; k < count; ++k) {
            const unsigned i = order[k];
            const bool was = next[i].shown;
            placed[k] = decide(labels[i], next[i], placed, k, seconds);
            changes += next[i].shown != was && !next[i].fresh;
            const float weight = fade(next[i], seconds);
            nameAlpha[i] = labels[i].hasName() ? weight : 0;
        }
        for (unsigned i = 0; i < count; ++i) memory_[i] = next[i];
        count_ = count;
        return changes;
    }

private:
    struct Memory {
        std::uint16_t name;
        float worldX, worldZ;
        bool shown, fresh;
        float weight, clearFor;
    };

    // Last frame's state for this label; a new label with room shows its name at once.
    Memory recall(const Label& label, bool* used) const {
        unsigned best = count_;
        float bestSq = kNameMatchMeters * kNameMatchMeters;
        for (unsigned j = 0; j < count_; ++j) {
            if (used[j] || memory_[j].name != label.name) continue;
            const float dx = memory_[j].worldX - label.worldX, dz = memory_[j].worldZ - label.worldZ;
            if (dx * dx + dz * dz <= bestSq) { bestSq = dx * dx + dz * dz; best = j; }
        }
        Memory memory{label.name, 0, 0, false, true, 0, kNameReturnSeconds};
        if (best < count_) { used[best] = true; memory = memory_[best]; memory.fresh = false; }
        memory.worldX = label.worldX; memory.worldZ = label.worldZ;
        return memory;
    }

    // Names already showing go first, so labels at similar distances do not trade names.
    static void placementOrder(const Label* labels, const Memory* memory, unsigned count, unsigned* order) {
        unsigned ordered = 0;
        for (int pass = 0; pass < 2; ++pass) {
            const unsigned start = ordered;
            for (unsigned i = 0; i < count; ++i) {
                if (memory[i].shown != (pass == 0)) continue;
                unsigned j = ordered++;
                while (j > start && labels[order[j - 1]].distanceSq > labels[i].distanceSq) { order[j] = order[j - 1]; --j; }
                order[j] = i;
            }
        }
    }

    static LabelBox decide(const Label& label, Memory& memory, const LabelBox* placed, unsigned placedCount,
                           float seconds) {
        const bool was = memory.shown;
        LabelBox full = label.icon;
        if (label.hasName()) full.x1 = label.nameRight;
        const LabelBox test = grown(full, memory.shown ? -kNameBand : kNameBand);
        bool clear = label.hasName();
        for (unsigned j = 0; j < placedCount && clear; ++j) clear = !overlaps(test, placed[j]);
        if (!label.hasName() || memory.shown) {
            memory.shown = clear;
        } else {
            memory.clearFor = clear ? memory.clearFor + seconds : 0;
            memory.shown = memory.clearFor >= kNameReturnSeconds;
        }
        if (was && !memory.shown) memory.clearFor = 0;
        return memory.shown ? full : label.icon;
    }

    static float fade(Memory& memory, float seconds) {
        const float step = memory.shown ? seconds / kNameFadeSeconds : -seconds / kNameFadeSeconds;
        const float weight = memory.fresh ? (memory.shown ? 1.f : 0.f) : memory.weight + step;
        memory.weight = weight < 0 ? 0 : weight > 1 ? 1 : weight;
        return memory.weight;
    }

    Memory memory_[kMaxLabels]{};
    unsigned count_{};
};
}
