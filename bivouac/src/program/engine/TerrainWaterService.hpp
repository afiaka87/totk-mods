// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include "MotherBase.hpp"

#include <cstdint>

namespace bivouac::engine {

// Guarded bridge to the MainField terrain-water API; the scene pointer lives for one attempt.
class TerrainWaterService {
public:
    void begin(std::uintptr_t mainBase);
    bool sample(float x, float z, motherbase::WaterSample* output);

    static bool query(
        void* context, float x, float z, motherbase::WaterSample* output);

private:
    bool resolveScene();

    std::uintptr_t mMainBase = 0;
    void* mScene = nullptr;
};

} // namespace bivouac::engine
