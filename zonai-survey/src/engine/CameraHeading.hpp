// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <atomic>

namespace zonai_survey::engine {

inline std::atomic<float> g_cameraForwardX{0.0f};
inline std::atomic<float> g_cameraForwardZ{1.0f};
inline std::atomic<bool> g_cameraForwardValid{false};

inline std::atomic<float> g_cameraPitch{0.0f};

inline void publishCameraForward(float x, float y, float z) {

    const float fullLengthSq = x * x + y * y + z * z;
    if (fullLengthSq > 0.0001f) {
        float sine = y / __builtin_sqrtf(fullLengthSq);
        if (sine > 1.0f) sine = 1.0f;
        if (sine < -1.0f) sine = -1.0f;
        g_cameraPitch.store(__builtin_asinf(sine), std::memory_order_relaxed);
    }

    const float lengthSq = x * x + z * z;

    if (!(lengthSq > 0.0001f)) return;
    const float inverse = 1.0f / __builtin_sqrtf(lengthSq);
    g_cameraForwardX.store(x * inverse, std::memory_order_relaxed);
    g_cameraForwardZ.store(z * inverse, std::memory_order_relaxed);
    g_cameraForwardValid.store(true, std::memory_order_release);
}

inline void cameraForward(float& outX, float& outZ) {
    outX = g_cameraForwardX.load(std::memory_order_relaxed);
    outZ = g_cameraForwardZ.load(std::memory_order_relaxed);
}

inline float cameraPitch() {
    return g_cameraPitch.load(std::memory_order_relaxed);
}

inline bool cameraForwardValid() {
    return g_cameraForwardValid.load(std::memory_order_acquire);
}

}
