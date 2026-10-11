// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
namespace stasis::runtime {
// Runs on each controller read: activation, targeting, the hold timer, hits and drawn state.
void tick(void *device);
} // namespace stasis::runtime
