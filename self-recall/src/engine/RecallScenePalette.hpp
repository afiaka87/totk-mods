#pragma once

#include <cstdint>

namespace self_recall::palette {

void install(std::uintptr_t mainBase);
void beginFrame(std::uint64_t epoch, std::uint32_t animationGeneration);
void afterNativeModel(const void* model);
std::uint64_t takeFailure();
const char* failureName(std::uint64_t failure);

} // namespace self_recall::palette
