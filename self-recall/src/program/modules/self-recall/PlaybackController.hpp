#pragma once

#include "RecallRuntime.hpp"
#include "RecallStopPolicy.hpp"
#include "totk/engine/Npad.hpp"

namespace self_recall::playback {

void start(RecallRuntime& runtime);

void step(RecallRuntime& runtime);

void finish(RecallRuntime& runtime, pure::PlaybackStop stop,
            bool emitEnd);

void applyRecordedInput(const RecallRuntime& runtime,
                        const totk::engine::NpadFrame& frame);

bool clearVelocity(const char* reason);

}  // namespace self_recall::playback
