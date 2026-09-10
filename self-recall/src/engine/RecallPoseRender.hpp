#pragma once

#include <span>
#include "RecallModelView.hpp"
#include "RecallRenderFrames.hpp"

namespace self_recall::pose_render {
bool copyBone(const void* unit, unsigned bone, float out[12]);

void install(std::uintptr_t mainBase);
void beginFrame(std::uint64_t epoch);
std::uint32_t latchedGeneration(std::uint64_t epoch);
enum class PaletteModel : unsigned { Foreign, Ready, Unavailable };
bool drawVisible(const void* renderUnit);
PaletteModel paletteModel(const void* unit, std::uint64_t epoch, std::uint32_t generation,
                          unsigned* failureDetail = nullptr);
bool copyWrist(std::uint32_t historyGeneration, pure::RenderWristFrame& out);
bool copyHeader(std::uint64_t epoch, std::uint32_t generation, pure::PoseFrameHeader& out);
pure::RenderFrameStore::Lease currentFrame(const void* body, std::uint64_t epoch);
void suppressEquipment(std::uint64_t epoch, std::span<const model::View> live);
void admitModels(void* scene, std::uint64_t epoch, std::span<const model::View> current,
                 std::span<const void* const> roots);
void verifyComplete(std::uint64_t epoch, const pure::RecordedPoseFrame& recorded,
             std::span<const model::View> current);
void collectorFailed(unsigned reason, unsigned detail);
void begin();
bool ready(std::uint32_t generation);
std::uint64_t takeFailure();
void reset();

} // namespace self_recall::pose_render
