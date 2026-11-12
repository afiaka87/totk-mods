// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
namespace stasis::runtime {
// Runs on each controller read: activation, targeting, the hold timer, hits and drawn state.
void tick(void *device);
#ifdef TOTK_SUITE_MEMBER
// Zonai Fieldworks' switch. Off: the next tick lets a frozen object go without a launch, and nothing new starts.
void setSwitchedOn(bool on);
bool switchedOn();
#endif
} // namespace stasis::runtime
