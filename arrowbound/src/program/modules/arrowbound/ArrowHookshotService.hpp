// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

#include "HookshotRuntime.hpp"

namespace arrowbound::arrow_hookshot {

void onArrowRelease(void* equipmentUser);
void onArrowUpdate(void* controller);
void onArrowSample(void* controller);
bool isTracked(void* controller);
void onArrowImpact(void* controller, bool classified, int hitType,
                   const float* adjustedHit, const void* motionContext);
void onArrowWorldImpact(void* controller, bool hit, bool water);

void service(HookshotRuntime& runtime, bool cancel, bool reaim, bool allowShots = true);
void reset(HookshotRuntime& runtime);
bool engaged(const HookshotRuntime& runtime);
bool keepsParagliderPresented();

}  // namespace arrowbound::arrow_hookshot
