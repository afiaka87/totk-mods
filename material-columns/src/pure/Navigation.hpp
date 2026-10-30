// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cmath>

namespace material_columns {
enum class Axis { none, horizontal, vertical };
struct Move { Axis axis = Axis::none; int direction{}; };

// Direction eligibility comes from the native processed input, so its dead zone applies.
// Lock the dominant axis for one gesture. Releasing that axis permits the next gesture.
class Navigation {
public:
    Move update(float x, float y, bool left, bool right, bool up, bool down,
                float frames, float firstRepeat, float laterRepeat) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(frames) || frames < 0 ||
            !(firstRepeat > 0) || !(laterRepeat > 0)) { reset(); return {}; }
        const int horizontal = left == right ? 0 : right ? 1 : -1;
        const int vertical = up == down ? 0 : down ? 1 : -1;
        if ((axis_ == Axis::horizontal && !horizontal) || (axis_ == Axis::vertical && !vertical)) reset();
        if (axis_ == Axis::none) {
            if (!horizontal && !vertical) return {};
            axis_ = !vertical || (horizontal && std::fabs(x) >= std::fabs(y)) ? Axis::horizontal : Axis::vertical;
        }
        const int direction = axis_ == Axis::horizontal ? horizontal : vertical;
        if (direction != direction_) {
            direction_ = direction;
            elapsed_ = frames;
            repeating_ = false;
            return {axis_, direction};
        }
        elapsed_ += frames;
        const float threshold = repeating_ ? laterRepeat : firstRepeat;
        if (elapsed_ < threshold) return {};
        // Native code performs at most one movement per update, retaining fractional time.
        elapsed_ -= threshold;
        repeating_ = true;
        return {axis_, direction};
    }
    void reset() { axis_ = Axis::none; direction_ = 0; elapsed_ = 0; repeating_ = false; }
private:
    Axis axis_ = Axis::none;
    int direction_{};
    float elapsed_{};
    bool repeating_{};
};
} // namespace material_columns
