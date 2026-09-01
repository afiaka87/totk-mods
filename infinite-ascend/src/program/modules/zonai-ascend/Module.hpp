#pragma once

#include <lib.hpp>

namespace zonai_ascend {

void init(uintptr_t mainBase, bool leniencyHookHealthy,
          bool markerScaleHooksHealthy);

u32 validationSpanBits();
float currentReach();
float markerSpan();

bool resolveQueryValid(void* manager, bool nativePassed);

void beginMarkerPostCalc(void* manager, void* updateContext);
void endMarkerPostCalc(void* manager);
void applyMarkerScale(void* handle, const void* position);

} // namespace zonai_ascend
