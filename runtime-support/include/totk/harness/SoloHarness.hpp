// SPDX-License-Identifier: GPL-2.0-only
//
// Shared TotK runtime support - solo module harness.
// Copyright (C) HanSoloBlaster, afiaka87
//
// Runs exactly one wwpg::Module: init(mainBase) + enter() once, then tick() per
// npad tick. Presentation (instruction banner, status-change banner, debug HUD)
// is compiled by SOLO_HARNESS_TEXT (default 1); at 0 the harness is lifecycle-only
// and never touches the overlay, so the TextWriter frame heap is never created and
// the text half of the debug-draw hook stays inert (the ship profile proven by
// Self Recall v1.0.0/v1.0.1 - native world-draw callbacks keep working).

#pragma once

#include <cstdint>

#include "totk/harness/Module.hpp"

namespace solo {
void init(std::uintptr_t mainBase, const wwpg::Module& module);
void tick(void* npadDevice);
}
