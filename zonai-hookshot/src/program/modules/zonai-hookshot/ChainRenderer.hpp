// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// A try-locked value snapshot connects tick and render threads.
#pragma once

#include <stdint.h>

namespace zonai_hookshot::render {
// The style byte matches pure::ChainStyle.
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

// Skip the frame if the snapshot is unavailable or locked.
bool takeSnapshot(Snapshot& out);

}  // namespace zonai_hookshot::render
