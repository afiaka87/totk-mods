// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Observe native Parasail; force its entry predicate only during an armed handoff.
#pragma once

#include "HookshotRuntime.hpp"

namespace zonai_hookshot::parasail {
void onHandoffArmed(HookshotRuntime& runtime);
void onGlideEntered(HookshotRuntime& runtime);
void onGlideRefused(HookshotRuntime& runtime);
void onGlideEnded(HookshotRuntime& runtime);

void serviceGlideHandoff(HookshotRuntime& runtime, pure::MachineInputs& inputs);
void serviceGlideTerminal(HookshotRuntime& runtime, pure::MachineInputs& inputs);

void onEnterHook(void* action);
void onUpdateHook(void* action);
void onLeaveHook(void* action);

// Shared paraglider-entry predicate: native result first, upgraded only while forceEntry is armed.
bool onGlideEntryPredicate(bool originalResult);

}  // namespace zonai_hookshot::parasail
