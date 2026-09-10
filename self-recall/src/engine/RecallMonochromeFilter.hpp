#pragma once
#include <cstdint>

namespace self_recall::monochrome {
void install(std::uintptr_t mainBase);
void initializeForScene(const void* extension);
void beginFrame(std::uint64_t epoch, std::uint32_t generation);
void protectHistoricalModel(void* model);
std::uint64_t takeFailure();
} // namespace self_recall::monochrome
