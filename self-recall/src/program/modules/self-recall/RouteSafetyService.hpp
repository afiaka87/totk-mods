#pragma once

#include "RecallRuntime.hpp"
#include "totk/harness/Module.hpp"

namespace self_recall::safety {

constexpr int kWarmupTicks = 30;
constexpr int kClimbGraceTicks = 45;
constexpr float kUprightMin = 0.7f;

Unsafe classify(RecallRuntime& runtime);

void serviceProbe(RecallRuntime& runtime);
void armProbe(RecallRuntime& runtime);

void observeRaycast(wwpg::RaycastFn original, const void* from,
                    const void* object);

}  // namespace self_recall::safety
