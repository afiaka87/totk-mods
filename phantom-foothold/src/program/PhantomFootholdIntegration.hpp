#pragma once

#include <cstdint>

namespace phantom::integration {

// Installs both loader hooks, or nothing when no native profile matches; used standalone and by suites.
[[nodiscard]] bool install(std::uintptr_t mainBase);

}  // namespace phantom::integration
