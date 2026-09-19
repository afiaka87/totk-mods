// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "TerrainWaterService.hpp"

#include <cstddef>

namespace bivouac::engine {
namespace {

constexpr std::ptrdiff_t kTerrainModuleIndirectOffset = 0x0462E178;
constexpr std::ptrdiff_t kSearchTerrainSceneByNameOffset = 0x01334BAC;
constexpr std::ptrdiff_t kGetWaterDepthOffset = 0x00CE8C94;

bool validPointer(std::uintptr_t value) {
    return value >= 0x1000000ull && value < 0x8000000000ull
        && (value & 0x3ull) == 0;
}

bool finiteHeight(float value) {
    return value == value && value > -10000.0f && value < 10000.0f;
}

} // namespace

void TerrainWaterService::begin(std::uintptr_t mainBase) {
    mMainBase = mainBase;
    mScene = nullptr;
}

bool TerrainWaterService::resolveScene() {
    if (mScene != nullptr) {
        return true;
    }
    if (mMainBase == 0) {
        return false;
    }

    const auto holder = *reinterpret_cast<const std::uintptr_t*>(
        mMainBase + kTerrainModuleIndirectOffset);
    if (!validPointer(holder)) {
        return false;
    }
    const auto module =
        *reinterpret_cast<const std::uintptr_t*>(holder);
    if (!validPointer(module)) {
        return false;
    }

    const char* sceneName = "MainField";
    const auto searchScene =
        reinterpret_cast<void* (*)(void*, const char**)>(
            mMainBase + kSearchTerrainSceneByNameOffset);
    mScene = searchScene(reinterpret_cast<void*>(module), &sceneName);
    return validPointer(reinterpret_cast<std::uintptr_t>(mScene));
}

bool TerrainWaterService::sample(
    float x, float z, motherbase::WaterSample* output) {
    if (output == nullptr) {
        return false;
    }
    *output = {};
    if (!resolveScene()) {
        return false;
    }

    float heights[2] = {0.0f, 0.0f};
    const float position[2] = {x, z};
    const auto getWaterDepth =
        reinterpret_cast<s32 (*)(float*, const float*, void*, s32, u32)>(
            mMainBase + kGetWaterDepthOffset);
    const s32 code =
        getWaterDepth(heights, position, mScene, -1, 0);
    output->nativeCode = code;
    output->hasWater = code == 1 || code == 2;
    if (!output->hasWater) {
        return true;
    }
    if (!finiteHeight(heights[0]) || !finiteHeight(heights[1])
        || heights[0] < heights[1]) {
        return false;
    }

    output->surfaceY = heights[0];
    output->bottomY = heights[1];
    output->depth = heights[0] - heights[1];
    return true;
}

bool TerrainWaterService::query(
    void* context, float x, float z,
    motherbase::WaterSample* output) {
    if (context == nullptr) {
        return false;
    }
    return static_cast<TerrainWaterService*>(context)->sample(
        x, z, output);
}

} // namespace bivouac::engine
