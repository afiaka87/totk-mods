#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace test_platform {
inline std::uint64_t now = 1000000;
inline std::vector<std::string> logs;
inline bool contains(const std::string& needle) {
    for (const auto& line : logs) if (line.find(needle) != std::string::npos) return true;
    return false;
}
}
