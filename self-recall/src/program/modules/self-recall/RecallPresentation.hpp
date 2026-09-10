#pragma once

#include <cstdint>

namespace self_recall::presentation {

void initialize(std::uintptr_t mainBase);
bool start(void* playerActor, std::uint32_t historyGeneration);
void stop(void* playerActor, const char* reason, bool emitEnd);
bool active();
void service();

}  // namespace self_recall::presentation
