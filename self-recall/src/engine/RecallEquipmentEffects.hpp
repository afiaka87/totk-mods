#pragma once
#include "RecallEquipmentEffectState.hpp"
#include "RecallModelView.hpp"

namespace self_recall::equipment_effects {
struct Values {
    std::uint32_t count = 0;
    float scale[3]{1, 1, 1};
    std::uint32_t properties[128]{};
};
void captureValues(unsigned asset, Values& out);
bool applyValues(unsigned asset, const Values& values);
void install(std::uintptr_t mainBase);
bool retain(unsigned asset, const void* actor, const void* copiedRoot);
void retire(unsigned asset);
void record(unsigned asset, pure::PoseFrameHeader& header);
void selectFrame(const pure::RecordedPoseFrame* frame);
void publishLive(std::span<const void* const> actors);
bool copyMatrix(const void* descriptor, float out[12]);
} // namespace self_recall::equipment_effects
