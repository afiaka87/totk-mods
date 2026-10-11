// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "StasisCore.hpp"
#include <cmath>
namespace stasis {
constexpr unsigned kTrailSamples = 96;
struct TrailSample {
    Vec3 position{};
    float birth{};
};
// Positions of a launched object; the runtime checks the actor that feeds it.
struct Trail {
    TrailSample samples[kTrailSamples]{};
    unsigned count{};
    float age{-1.0f};
    void reset() {
        count = 0;
        age = -1.0f;
    }
    void begin(Vec3 p) {
        reset();
        age = 0;
        record(p);
    }
    bool emitting() const { return age >= 0 && age <= 3.0f; }
    bool visible() const { return age >= 0 && count > 0; }
    void advance(float frames) {
        if (age < 0 || !std::isfinite(frames) || frames <= 0)
            return;
        age += frames / 30.0f;
        if (age > 4.9f)
            reset();
    }
    void record(Vec3 p) {
        if (!emitting() || !finite(p))
            return;
        if (count && age - samples[count - 1].birth < 1.0f / 35.0f)
            return;
        if (count && length(sub(p, samples[count - 1].position)) > 25.0f) {
            reset();
            return;
        }
        if (count == kTrailSamples) {
            for (unsigned i = 1; i < count; ++i)
                samples[i - 1] = samples[i];
            --count;
        }
        samples[count++] = {p, age};
    }
};
} // namespace stasis
