#pragma once

#include <cstdint>
#include "RecallFrameHooks.hpp"

namespace self_recall::pose_recorder {

struct Control {
    std::uintptr_t scene = 0;
    std::uintptr_t player = 0;
    std::uint64_t tick = 0;
    std::uint32_t worldGeneration = 0;
    std::int32_t stickX = 0;
    std::int32_t stickY = 0;
    std::uint8_t sampleFlags = 0;
    bool climbMayAdmit = false;
    bool vehicleMayAdmit = false;
    bool enabled = false;
};

void install(std::uintptr_t mainBase);
void publishControl(const Control& control);
void beginFrame(std::uint64_t epoch);
void modelsComplete(const frame::CompletedModelPhase& phase);
void prepareScene(void* scene, std::uint64_t epoch);

bool presentationPose(void* actor, pure::Pose& out);

bool trySuspend();
bool clearPending();
void resume(bool clearHistory);
void logDiagnostics();

}  // namespace self_recall::pose_recorder
