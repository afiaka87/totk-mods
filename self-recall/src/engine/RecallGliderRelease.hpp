#pragma once

#include <cstdint>
#include "RecallGliderPolicy.hpp"

namespace self_recall::glider_release {
void install(std::uintptr_t mainBase);
bool nativeGliding(std::uint32_t actorId);
bool nativeClimbing(std::uint32_t actorId);
void request(void* player, std::uint32_t worldGeneration, std::uint64_t tick,
             bool recordedNativeGlide);
void service(void* player, std::uint32_t worldGeneration, std::uint64_t tick,
             bool allowed, bool userCancelled);
void cancel(pure::GliderReleaseEnd reason = pure::GliderReleaseEnd::Cancelled);
void resetWorld();
} // namespace self_recall::glider_release
