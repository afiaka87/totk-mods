#pragma once

#include <cstdint>

namespace zonai_ascend {

void init(std::uintptr_t mainBase, bool leniencyHookHealthy,
          bool markerScaleHooksHealthy, std::ptrdiff_t setPosAndScaleOffset,
          std::ptrdiff_t actorPositionOffset);

std::uint32_t validationSpanBits();
float currentReach();
float markerSpan();

bool resolveQueryValid(void* manager, bool nativePassed);

void beginMarkerPostCalc(void* manager, void* updateContext);
void endMarkerPostCalc(void* manager);
void applyMarkerScale(void* handle, const void* position);

}
