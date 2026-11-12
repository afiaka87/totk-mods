// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
// Host-only logging seam for exercising the real wall-grip service without the Switch SDK.
struct TestLog { void Log(const char*, ...) {} };
inline TestLog Logging;
