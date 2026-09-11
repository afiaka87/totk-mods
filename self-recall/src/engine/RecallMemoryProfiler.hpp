#pragma once
#include <cstdint>
#include "RecallPoseHistory.hpp"
#ifndef SELF_RECALL_MEMORY_PROFILE
#define SELF_RECALL_MEMORY_PROFILE 0
#endif
namespace self_recall::memory_profile {
#if SELF_RECALL_MEMORY_PROFILE
void install(std::uintptr_t mainBase);
void watchArchive(void* heap);
void recordPose(const pure::PoseHistory& history, pure::PoseFrameKey key);
void recordCaptureTicks(std::uint64_t ticks);
#else
inline void install(std::uintptr_t) {}
inline void watchArchive(void*) {}
inline void recordPose(const pure::PoseHistory&, pure::PoseFrameKey) {}
inline void recordCaptureTicks(std::uint64_t) {}
#endif
}
