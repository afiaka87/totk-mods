// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "HookshotRuntime.hpp"

namespace arrowbound::wall_grip {
bool begin(HookshotRuntime& rt, pure::Vec3 contact, pure::Vec3 direction);
void stop(HookshotRuntime& rt);
// nullptr means still working; "climb" means acquired; every other value is a bailout reason.
const char* service(HookshotRuntime& rt);
void onParasailUpdate(void* action);
void onClimbUpdate(void* action);
void onControllerUpdate(void* controller);
} // namespace arrowbound::wall_grip
