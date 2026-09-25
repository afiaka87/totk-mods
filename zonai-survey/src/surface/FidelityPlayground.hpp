// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace survey_fidelity {
void install(std::uintptr_t mainBase);
void tick(void* device);
bool beginPulse(float x, float y, float z, float headingX, float headingZ);
void endPulse();
bool pulseRunning();
bool imprintEnabled();
float markerArrivalSeconds(float distance);
float pulseSeconds();
}
