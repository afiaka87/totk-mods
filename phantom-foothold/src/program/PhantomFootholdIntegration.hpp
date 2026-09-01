#pragma once

#include <cstdint>

namespace phantom::integration {

// Installs the released hooks after exlaunch is initialized; false when a guard word mismatches (nothing installed).
[[nodiscard]] bool install(std::uintptr_t mainBase);

}  // namespace phantom::integration
