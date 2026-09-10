#pragma once
#include <array>
#include "RecallHistory.hpp"

namespace self_recall::pure {
struct ControllerPoseOutput {
    float* matrix = nullptr;
    std::array<float*, 4> velocities{};

    bool playerCommit(std::uintptr_t player) const {
        if (!player || !matrix) return false;
        for (unsigned i = 0; i < velocities.size(); ++i)
            if (reinterpret_cast<std::uintptr_t>(velocities[i]) != player + 0x320u + 12u * i)
                return false;
        return true;
    }

    bool apply(const Pose& pose) const {
        if (!matrix || !finitePose(pose)) return false;
        for (const auto* velocity : velocities) if (!velocity) return false;
        const float position[]{pose.position.x, pose.position.y, pose.position.z};
        for (unsigned row = 0; row < 3; ++row) {
            for (unsigned column = 0; column < 3; ++column)
                matrix[row * 4 + column] = pose.rotation.values[row * 3 + column];
            matrix[row * 4 + 3] = position[row];
        }
        for (auto* velocity : velocities)
            for (unsigned axis = 0; axis < 3; ++axis) velocity[axis] = 0;
        return true;
    }
};
} // namespace self_recall::pure
