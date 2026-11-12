// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>
namespace arrowbound::model_trace {
#if ARROWBOUND_FLIGHT_DIAGNOSTICS
void install(std::uintptr_t base);
void begin(std::uint32_t trace);
void reset();
#else
inline void install(std::uintptr_t) {}
inline void begin(std::uint32_t) {}
inline void reset() {}
#endif
}
