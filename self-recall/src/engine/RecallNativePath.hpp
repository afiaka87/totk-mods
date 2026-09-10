#pragma once

#include "RecallPathFrame.hpp"

namespace sead { class Heap; }

namespace self_recall::native_path {

void install(std::uintptr_t mainBase);
void initializeForScene(void* extension, void* scene, sead::Heap* heap);
void beginFrame(std::uint64_t epoch, std::uint32_t generation);
bool publish(const pure::NativePathRoute& route);
std::uint64_t takeFailure();

} // namespace self_recall::native_path
