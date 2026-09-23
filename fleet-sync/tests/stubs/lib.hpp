#pragma once
#include "TestPlatform.hpp"
inline std::uint64_t svcGetSystemTick() { return test_platform::now; }
