#pragma once

#include "RecallRuntime.hpp"

namespace self_recall::effects {

void initialize(std::uintptr_t mainBase);

void service(RecallRuntime& runtime);

void buildRoute(RecallRuntime& runtime);

bool startRewind(RecallRuntime& runtime);

void driveAnimation(RecallRuntime& runtime);

void stopForExit(RecallRuntime& runtime, const char* reason, bool emitEnd,
                 bool graceful);

void abandonAll(RecallRuntime& runtime, const char* reason, bool releaseAnim);

}  // namespace self_recall::effects
