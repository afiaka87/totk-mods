// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cmath>
#include <cstdint>

namespace botw_abilities::champions {
struct GlideOwner {
    std::uint64_t actor{}, scene{}, generation{};
    constexpr bool operator==(const GlideOwner&) const = default;
};
enum class GlidePulse : std::uint8_t { None, Jump, Glide };
struct GlideSample {
    bool press{};
    GlidePulse started{};
};
// Jump edges span fresh samples (30 Hz world, 60 Hz pad); the airborne edge opens the glider.
struct GaleGlide {
    static constexpr int kReleaseSamples = 4, kPressSamples = 4, kJumpRetrySamples = 12;
    static constexpr int kJumpPresses = 2, kGlideGapSamples = 8, kGlidePresses = 3;
    GlideOwner owner{};
    float remaining{};
    int released{}, pressing{}, jumpPresses{}, glidePresses{}, ownJumps{};
    bool airborne{};
    void reset() { *this = {}; }
    void arm(GlideOwner value) {
        reset();
        owner = value;
        remaining = value.actor && value.scene ? 3.0f : 0;
    }
    // Never synthesizes an edge over a held X; the player's own press restarts the release wait.
    GlideSample injectJump(bool fresh, bool physicalJump, bool grounded) {
        if (remaining <= 0 || !fresh) return {};
        if (physicalJump) {
            released = pressing = 0;
            return {};
        }
        if (jumpPresses && !grounded) airborne = true;
        if (pressing > 0) {
            --pressing;
            return {true};
        }
        ++released;
        if (!airborne && jumpPresses < kJumpPresses &&
            released >= (jumpPresses ? kJumpRetrySamples : kReleaseSamples)) {
            ++jumpPresses;
            ++ownJumps;
            return start(GlidePulse::Jump);
        }
        if (airborne && glidePresses < kGlidePresses && released >= kGlideGapSamples) {
            ++glidePresses;
            return start(GlidePulse::Glide);
        }
        return {};
    }
    // The injected jump must not start another Gale sequence.
    bool claimJump() {
        const bool own = ownJumps > 0 && remaining > 0;
        if (own) --ownJumps;
        return own;
    }
    bool admits(GlideOwner value, bool gameplay, bool shrine, bool ownsGlider) const {
        return remaining > 0 && owner == value && gameplay && !shrine && ownsGlider;
    }
    bool advance(GlideOwner value, float seconds) {
        if (remaining <= 0) return false;
        if (!(owner == value)) { reset(); return false; }
        if (std::isfinite(seconds) && seconds > 0) remaining -= seconds;
        if (remaining > 0) return false;
        reset();
        return true;
    }
    bool entered(std::uint64_t actor) {
        if (remaining <= 0 || actor != owner.actor) return false;
        reset();
        return true;
    }

private:
    GlideSample start(GlidePulse pulse) {
        released = 0;
        pressing = kPressSamples - 1;
        return {true, pulse};
    }
};
} // namespace botw_abilities::champions
