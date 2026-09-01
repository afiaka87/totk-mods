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

// Installs the passive hooks; the host must call exl::hook::Initialize() first.
InstallStatus install(std::uintptr_t mainBase);

}  // namespace zonai_ascend::hooks
