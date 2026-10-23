// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cmath>
#include <cstdint>

namespace botw_abilities::champions {
struct PoseTime {
    std::uint32_t channel = 0;
    std::uint64_t tick = UINT64_MAX;
    float frames = 0;
};

inline PoseTime advancePoseTime(PoseTime previous, std::uint32_t channel,
                                std::uint64_t tick, float frames) {
    if (!channel) return {};
    if (channel != previous.channel) previous = {channel};
    if (previous.tick == tick || !std::isfinite(frames) || frames < 0 || frames >= 100) return previous;
    previous.tick = tick;
    previous.frames += frames;
    return previous;
}

// The crouch clip spans the whole charge so Link settles as Gale becomes ready; loops keep their own pace.
inline float channelPoseFrames(float frames, float introFrames, float channelFrames) {
    if (!(introFrames > 0) || !(channelFrames > introFrames) || !std::isfinite(channelFrames)) return frames;
    if (frames < channelFrames) return frames * (introFrames / channelFrames);
    return frames - channelFrames + introFrames;
}
} // namespace botw_abilities::champions
