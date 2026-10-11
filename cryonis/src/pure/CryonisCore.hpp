// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "CryonisTuning.hpp"

namespace cryonis {
// BotW's pillar limit, placement reach in metres and cooldown in gameplay frames.
inline constexpr unsigned kPillarLimit = 3;
inline constexpr float kReach = 60.0f, kCooldownFrames = 50.0f;
struct Vec3 {
    float x{}, y{}, z{};
};
inline bool finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 mul(Vec3 a, float b) { return {a.x * b, a.y * b, a.z * b}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 p) { return std::sqrt(dot(p, p)); }
inline float distance(Vec3 a, Vec3 b) { return length(sub(a, b)); }
inline Vec3 rotateYaw(Vec3 p, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    return {c * p.x + s * p.z, p.y, -s * p.x + c * p.z};
}
inline float faceLink(Vec3 base, Vec3 link, float fallback = 0) {
    const float x = link.x - base.x, z = link.z - base.z;
    return x * x + z * z > .0001f ? std::atan2(x, z) : fallback;
}
inline Vec3 footprintPoint(Vec3 base, float x, float y, float z, float yaw) {
    return add(base, rotateYaw({x, y, z}, yaw));
}
struct Ray {
    Vec3 eye{}, direction{};
    bool valid{};
};
struct Water {
    float surface{}, bottom{};
    bool present{};
};
struct Target {
    Vec3 base{}, normal{0, 1, 0};
    bool valid{};
    int pillar{-1};
    float yaw{};
};
inline bool actionReady(Target target, float cooldown, bool pending) {
    return target.valid && !pending && (target.pillar >= 0 || cooldown <= 0);
}
struct Animation {
    float frame{};
    // Candidate rates follow Appear_Linear.bas hold 47; its native meaning is unverified.
    void advance(float elapsed) {
        if (!std::isfinite(elapsed) || elapsed <= 0)
            return;
        while (elapsed > 0 && frame < 100) {
            const float end = frame < 6 ? 6.0f : frame < 10 ? 10.0f : frame < 90 ? 90.0f : 100.0f;
            const float rate = frame < 6 ? 2.0f : frame < 10 ? 4.0f : frame < 90 ? 5.0f : 1.0f;
            const float amount = std::min(elapsed, (end - frame) / rate);
            frame = std::min(frame + amount * rate, end);
            elapsed -= amount;
        }
    }
    bool finished() const { return frame >= 100; }
};
struct Pillar {
    Vec3 base{};
    std::uint64_t id{};
    Animation growth{};
    bool occupied{};
    float yaw{};
};
struct Reservation {
    unsigned slot{};
    std::uint64_t remove{};
    bool valid{};
};
struct State {
    Pillar pillars[kPillarLimit]{};
    float cooldown{}, previewFrames{};
    std::uint64_t nextId{1};
    bool aiming{};
    void tick(float frames, bool running) {
        if (!running || !std::isfinite(frames) || frames <= 0 || frames > 100)
            return;
        cooldown = std::max(0.0f, cooldown - frames);
        if (aiming)
            previewFrames += frames;
        for (auto &p : pillars)
            if (p.occupied)
                p.growth.advance(frames);
    }
    Reservation reserve(const Target &t) const {
        if (!aiming || cooldown > 0 || !t.valid || t.pillar >= 0 || !finite(t.base))
            return {};
        for (unsigned i = 0; i < kPillarLimit; ++i)
            if (!pillars[i].occupied)
                return {i, 0, true};
        unsigned oldest = 0;
        for (unsigned i = 1; i < kPillarLimit; ++i)
            if (pillars[i].id < pillars[oldest].id)
                oldest = i;
        return {oldest, pillars[oldest].id, true};
    }
    // Commit only after the new native actor is ready; failed creation retains the oldest.
    bool commit(Reservation r, Target t) {
        if (!r.valid || r.slot >= kPillarLimit || !t.valid || !finite(t.base))
            return false;
        const auto &old = pillars[r.slot];
        if ((old.occupied ? old.id : 0) != r.remove)
            return false;
        pillars[r.slot] = {t.base, nextId++, {}, true, t.yaw};
        cooldown = kCooldownFrames;
        aiming = false;
        previewFrames = 0;
        return true;
    }
    bool remove(std::uint64_t id) {
        for (auto &p : pillars)
            if (p.occupied && p.id == id) {
                p = {};
                return true;
            }
        return false;
    }
    void reset() { *this = {}; }
};
// Query actual water along the view ray; never use one unbounded water plane.
template <class Sample> Target waterTarget(Ray r, Sample sample) {
    if (!r.valid || !finite(r.eye) || !finite(r.direction))
        return {};
    const float magnitude = length(r.direction);
    if (magnitude < 0.001f)
        return {};
    r.direction = mul(r.direction, 1 / magnitude);
    float previous = 0;
    for (float d = 0.5f; d <= kReach; d += 0.5f) {
        const auto p = add(r.eye, mul(r.direction, d));
        const Water water = sample(p.x, p.z);
        if (!water.present || !std::isfinite(water.surface) || !std::isfinite(water.bottom) ||
            water.surface < water.bottom) {
            previous = d;
            continue;
        }
        if (p.y <= water.surface && r.direction.y < 0) {
            float lo = previous, hi = d;
            for (unsigned step = 0; step < 12; ++step) {
                const float mid = (lo + hi) * 0.5f;
                const auto q = add(r.eye, mul(r.direction, mid));
                const auto w = sample(q.x, q.z);
                if (w.present && q.y <= w.surface)
                    hi = mid;
                else
                    lo = mid;
            }
            const auto hit = add(r.eye, mul(r.direction, hi));
            const auto w = sample(hit.x, hit.z);
            if (!w.present || std::fabs(hit.y - w.surface) > 0.03f)
                return {};
            return {{hit.x, w.surface, hit.z}, {0, 1, 0}, true, -1};
        }
        previous = d;
    }
    return {};
}
}
