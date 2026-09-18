// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Tick-to-render mailbox: one snapshot published by value, copied back under a try-lock, no game
// object retained.
#pragma once

#include <stdint.h>

namespace zonai_hookshot::render {
// Styles mirror pure::ChainStyle as a raw byte so this bridge stays independent of the pure
// headers.
struct Snapshot {
    uint32_t visible = 0;
    float nearPoint[3]{};
    float farPoint[3]{};
    float reticlePoint[3]{};
    uint32_t latchFeedbackTicks = 0;
    uint8_t style = 0;  // pure::ChainStyle values
};

// Call once during module init.
void configure(uintptr_t mainBase);

// Tick-thread publication.
void publish(const Snapshot& snapshot);

// Render-thread read; false when the lock is held or nothing is published, and the caller skips
// the frame.
bool takeSnapshot(Snapshot& out);

}  // namespace zonai_hookshot::render
