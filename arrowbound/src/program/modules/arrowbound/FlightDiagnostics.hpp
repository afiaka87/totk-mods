// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "HookshotRuntime.hpp"

namespace arrowbound::diagnostics {
#if ARROWBOUND_FLIGHT_DIAGNOSTICS
void release(bool enabled);
void claim(void* controller, std::uint32_t state, bool playerOwned);
bool observes(void* controller);
void sample(void* controller, pure::Vec3 position, pure::Vec3 velocity, float rate, float dt);
void impact(void* controller, const pure::ArrowImpact& impact);
void gameplay(const HookshotRuntime& rt);
void reset(const char* reason);
#else
inline void release(bool) {}
inline void claim(void*, std::uint32_t, bool) {}
inline bool observes(void*) { return false; }
inline void sample(void*, pure::Vec3, pure::Vec3, float, float) {}
inline void impact(void*, const pure::ArrowImpact&) {}
inline void gameplay(const HookshotRuntime&) {}
inline void reset(const char*) {}
#endif
}
