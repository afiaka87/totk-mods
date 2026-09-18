// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Getting the paraglider open, then leaving it alone: native Parasail is observed, never written;
// only the Fall selector's entry predicate is forced, and only while a handoff is armed.
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
