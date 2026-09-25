#pragma once

#include <cstdint>

namespace zonai_ascend::hooks {

struct InstallStatus {
    bool range = false;
    bool leniency = false;
    bool markerScale = false;

    [[nodiscard]] bool fullyHealthy() const {
        return range && leniency && markerScale;
    }
};

// Initialize exlaunch hooks before calling install.
InstallStatus install(std::uintptr_t mainBase);

}
