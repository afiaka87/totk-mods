// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87

#pragma once

namespace arrowbound::parasail {
void onFallEnterHook(void* action);
void onFallUpdateHook(void* action);
void onFallLeaveHook(void* action);
void onEnterHook(void* action);
void onUpdateHook(void* action);
void onLeaveHook(void* action);
bool onGlideEntryPredicate(bool originalResult);
}
