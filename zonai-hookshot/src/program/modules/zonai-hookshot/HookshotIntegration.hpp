// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87

// Fieldworks entry seam: starts targeting directly, without the standalone chord.
#pragma once

namespace zonai_hookshot::integration {
bool beginTargeting();
bool movementEngaged();
bool worldReady();

// Combined host control; legacy hosts keep unrestricted activation by default.
bool ownsMovement();
void yieldMovement();
void allowActivation(bool allowed);

}  // namespace zonai_hookshot::integration
