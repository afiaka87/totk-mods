// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace botw_abilities::champions::audio {

struct Handle {
    std::int8_t bank{};
    std::uint8_t padding{};
    std::int16_t index{-1};
    std::uint32_t serial{};
    explicit operator bool() const { return index >= 0; }
};
static_assert(sizeof(Handle) == 8);

template <std::size_t Count>
class Playback {
    std::array<Handle, Count> handles_{};

public:
    template <class Emit, class Fade>
    bool play(std::size_t cue, bool sustain, Emit emit, Fade fade) {
        if (cue >= Count) return false;
        if (sustain && handles_[cue]) return true;
        // Keep the current sound if the native speaker cannot accept its replacement.
        const Handle next = emit(cue);
        if (!next) return false;
        stop(cue, fade);
        handles_[cue] = next;
        return true;
    }

    template <class Fade>
    void stop(std::size_t cue, Fade fade) {
        if (cue >= Count || !handles_[cue]) return;
        fade(handles_[cue]);
        handles_[cue] = {};
    }

    template <class Fade>
    void stopAll(Fade fade) {
        for (std::size_t cue = 0; cue < Count; ++cue) stop(cue, fade);
    }
};
} // namespace botw_abilities::champions::audio
