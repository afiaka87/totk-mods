// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Action and controller hooks delegate to the corresponding services.

// Action hooks run on the game's action threads and talk to the tick only through atomics.
#pragma once

#include "ClimbCapture.hpp"
#include "ParasailHandoff.hpp"
#include "TransportController.hpp"
#include "totk/harness/Module.hpp"

namespace wwpg::modules {
const Module& zonaiHookshot();
}

namespace zonai_hookshot::integration {
// Fieldworks reads this instead of status text; a quarantined stick click counts as engaged.
bool movementEngaged();

}
