#pragma once

#include <cstddef>
#include <cstdint>

#include "RecallPoseHistory.hpp"

namespace self_recall::pose_storage {

enum class State : std::uint8_t { WaitingForGameplay, Constructing, Ready };

struct Diagnostics {
    State state = State::WaitingForGameplay;
    std::size_t reservedBytes = 0;
};

void setPreparationAllowed(bool ready);
State prepare();

pure::PoseHistory* history();
Diagnostics diagnostics();

}  // namespace self_recall::pose_storage
