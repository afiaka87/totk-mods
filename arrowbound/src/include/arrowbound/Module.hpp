// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <cstdint>

namespace arrowbound {
using RaycastFn = std::uint64_t (*)(const void*, const void*, const void*,
                                  const void*, std::uint32_t, std::uint32_t);
void initialize(std::uintptr_t mainBase);
void enter();
void tick(void* device, bool allowShots = true);
void onRaycast(RaycastFn original, const void* from, const void* object);
bool movementEngaged();
void yieldMovement();
void installFeatureHooks(std::uintptr_t mainBase);
void sharedGripHooksReady(bool ready);

namespace parasail {
void onFallEnterHook(void* action);
void onFallUpdateHook(void* action);
void onFallLeaveHook(void* action);
void onEnterHook(void* action);
void onUpdateHook(void* action);
void onLeaveHook(void* action);
bool onGlideEntryPredicate(bool originalResult);
}
namespace wall_grip {
void onControllerUpdate(void* controller);
void onClimbUpdate(void* action);
}
}
