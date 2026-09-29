// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace zonai_survey::pulse {
void install(std::uintptr_t mainBase);
bool beginPulse(float x, float y, float z, float headingX, float headingZ);
void endPulse();
bool pulseRunning();
float markerArrivalSeconds(float distance);
float pulseSeconds();
}
