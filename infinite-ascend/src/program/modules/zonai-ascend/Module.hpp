#pragma once

#include <bit>
#include <cstdint>

#include "../../../pure/ReachConfig.hpp"

namespace zonai_ascend {

void init(std::uintptr_t mainBase, bool leniencyHookHealthy,
          bool markerScaleHooksHealthy, std::ptrdiff_t setPosAndScaleOffset,
          std::ptrdiff_t actorPositionOffset);

constexpr std::uint32_t validationSpanBits() {
    return std::bit_cast<std::uint32_t>(pure::deriveReach(pure::kReleaseReach).validationSpan);
}
constexpr float currentReach() { return pure::kReleaseReach; }
constexpr float markerSpan() { return pure::deriveReach(pure::kReleaseReach).markerSpan; }

bool resolveQueryValid(void* manager, bool nativePassed);

void beginMarkerPostCalc(void* manager, void* updateContext);
void endMarkerPostCalc(void* manager);
void applyMarkerScale(void* handle, const void* position);

}
