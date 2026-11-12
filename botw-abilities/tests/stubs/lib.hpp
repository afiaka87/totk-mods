// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
struct TestLogger {
    template<class... Args> void Log(const char*, Args...) const {}
};
inline TestLogger Logging;
