#pragma once
#include "TestPlatform.hpp"
#include <stdexcept>
struct TestLogger {
    template <class... Args> void Log(const char* format, Args... args) {
        char buffer[2048];
        const int size = std::snprintf(buffer, sizeof(buffer), format, args...);
        if (size < 0 || size >= 1024) throw std::runtime_error("diagnostic exceeds shipping log buffer");
        test_platform::logs.emplace_back(buffer);
    }
};
inline TestLogger Logging;
