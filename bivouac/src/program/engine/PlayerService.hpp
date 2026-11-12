// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87

#pragma once

#include <cstdint>

#include "runtime/BivouacState.hpp"

namespace bivouac::engine {

struct PlayerSnapshot {
    void* actor = nullptr;
    runtime::Vec3 position{};
    runtime::Vec3 forward{};
    bool climbing = false;
};

class PlayerService {
public:
    void setMainBase(std::uintptr_t mainBase) { mMainBase = mainBase; }

    void* resolve() const;
    PlayerSnapshot snapshot(void* player) const;
    void copyRotation(void* player, float output[9]) const;

private:
    std::uintptr_t mMainBase = 0;
};

} // namespace bivouac::engine
