#pragma once
#include "RecallModelView.hpp"

namespace self_recall::equipment {
void install(std::uintptr_t mainBase);
bool remap(const void* component, std::uint32_t actorId, std::uint32_t world,
           const void* playerComponent, std::span<model::View> source);
void beginRecord(std::uint32_t historyGeneration);
bool recorded(const pure::RecordedPoseFrame& frame);
bool selectAppearance(const pure::RecordedPoseFrame& frame);
void recordEffects(pure::PoseFrameHeader& header, std::span<const model::View> views);
void collectExpired(const pure::PoseHistory& history, std::uint32_t world);
bool publishedModel(const void* unit);

bool resolve(const pure::RecordedModelIdentity& token, const void* scene,
             const void* playerComponent, model::View& view, const void*& root);
} // namespace self_recall::equipment
