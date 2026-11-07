// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
struct TestLogger {
    template<class... Args> void Log(const char*, Args...) const {}
};
inline TestLogger Logging;
