// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Target validation over a copied raycast sample; nothing here reads game memory.

// A sample is only valid for the request that produced it (generation, sequence, freshness); commit also requires a
// sequence at or after the confirmation floor.
#pragma once

#include <cstdint>

#include "Vec3.hpp"

namespace zonai_hookshot::pure {
enum class Verdict : uint8_t {
    Pending,             // no fresh completed sample for the current request
    Miss,                // completed cast, nothing hit inside the cast segment
    InvalidFinite,       // non-finite/degenerate position, normal, range, or ray direction
    TooNear,
    TooFar,
    NoClimb,             // resolved user-shape NoClimb bit (result +0x50 bit 0)
    Floor,
    CeilingOrOverhang,
    Backface,
    MovingOrUnsupported, // movable (keyframed/dynamic) or unresolved hit body
    BlockedCorridor,     // reserved for corridor probes (post-P1)
    Valid,
};

inline const char* verdictName(Verdict v) {
    switch (v) {
        case Verdict::Pending:             return "SCANNING";
        case Verdict::Miss:                return "NO TARGET";
        case Verdict::InvalidFinite:       return "BAD SAMPLE";
        case Verdict::TooNear:             return "TOO NEAR";
        case Verdict::TooFar:              return "TOO FAR";
        case Verdict::NoClimb:             return "NO-CLIMB";
        case Verdict::Floor:               return "FLOOR";
        case Verdict::CeilingOrOverhang:   return "CEILING";
        case Verdict::Backface:            return "BACKFACE";
        case Verdict::MovingOrUnsupported: return "NOT TERRAIN";
        case Verdict::BlockedCorridor:     return "BLOCKED";
        case Verdict::Valid:               return "VALID";
    }
    return "?";
}

// Motion classes from the rigid-body read: 0 static world, 1 keyframed, 2 dynamic. Movable targets
// are refused: an anchor the chain cannot follow is worse than a refusal.
constexpr uint32_t kMotionStatic = 0;

struct TargetSample {
    bool completed = false;
    bool hit = false;
    Vec3 position{};
    Vec3 normal{};
    float range = 0.0f;
    uint32_t shapeFlags = 0;    // resolved user-shape field (result +0x50)
    Vec3 rayDirection{};        // camera ray at request time (engine sends unit)
    uint32_t generation = 0;    // world generation at request time
    uint32_t sequence = 0;      // request sequence that produced this result
    bool hitBodyKnown = false;  // engine resolved the hit body object (result +0x170)
    uint32_t hitMotionType = 0; // raw motion class of the hit body (HUD shows it)
};

// Normal band: reject floor-like and ceiling-like normals, accept walls including shallow
// underhangs.
struct ValidatorConfig {
    float minRange = 2.0f;
    // Cast past maxRange so a hit in (300, 350] reads TooFar rather than Miss.
    float maxRange = 300.0f;
    float floorNormalY = 0.65f;     // normalized normal.y >= this reads as a floor
    float ceilingNormalY = -0.45f;  // normalized normal.y <= this reads as ceiling/overhang
    float backfaceDotMax = -0.05f;  // require dot(unit normal, unit ray) <= this
    int previewMaxAgeTicks = 10;    // target diamond drops when the sample is older
    int confirmMaxAgeTicks = 6;     // commit-eligible sample must be this fresh
};

struct SampleContext {
    uint32_t currentGeneration = 0;
    uint32_t latestResultSequence = 0;  // sequence of the newest consumed result
    int sampleAgeTicks = 0;             // now - tick the sample was consumed
};

inline Verdict validate(const TargetSample& s, const SampleContext& ctx,
                        const ValidatorConfig& c = {}) {
    if (!s.completed || s.generation != ctx.currentGeneration ||
        s.sequence != ctx.latestResultSequence ||
        ctx.sampleAgeTicks > c.previewMaxAgeTicks)
        return Verdict::Pending;
    if (!s.hit) return Verdict::Miss;
    if (!finite3(s.position) || !finite3(s.normal) || !std::isfinite(s.range) ||
        !finite3(s.rayDirection))
        return Verdict::InvalidFinite;
    const float normalLength = length(s.normal);
    if (normalLength < 0.5f || normalLength > 1.5f) return Verdict::InvalidFinite;
    const float rayLength = length(s.rayDirection);
    if (rayLength < 0.5f || rayLength > 1.5f) return Verdict::InvalidFinite;
    if (!s.hitBodyKnown) return Verdict::MovingOrUnsupported;
    if (s.hitMotionType != kMotionStatic) return Verdict::MovingOrUnsupported;
    if (s.shapeFlags & 1u) return Verdict::NoClimb;
    if (s.range < c.minRange) return Verdict::TooNear;
    if (s.range > c.maxRange) return Verdict::TooFar;
    const Vec3 unitNormal = mul(s.normal, 1.0f / normalLength);
    const Vec3 unitRay = mul(s.rayDirection, 1.0f / rayLength);
    if (dot(unitNormal, unitRay) > c.backfaceDotMax) return Verdict::Backface;
    if (unitNormal.y >= c.floorNormalY) return Verdict::Floor;
    if (unitNormal.y <= c.ceilingNormalY) return Verdict::CeilingOrOverhang;
    return Verdict::Valid;
}

// The anchor may only come from a request issued at or after the A press.
enum class ConfirmDecision : uint8_t { Waiting, Commit, Refuse };

inline ConfirmDecision confirmDecision(const TargetSample& s, const SampleContext& ctx,
                                       uint32_t confirmFloorSequence,
                                       const ValidatorConfig& c = {}) {
    if (!s.completed || s.generation != ctx.currentGeneration ||
        s.sequence < confirmFloorSequence ||
        ctx.sampleAgeTicks > c.confirmMaxAgeTicks)
        return ConfirmDecision::Waiting;
    return validate(s, ctx, c) == Verdict::Valid ? ConfirmDecision::Commit
                                                 : ConfirmDecision::Refuse;
}

// A sample that never satisfied the confirmation gate reports Pending, never the old preview
// verdict.
inline Verdict confirmRefusalReason(const TargetSample& s, const SampleContext& ctx,
                                    uint32_t confirmFloorSequence,
                                    const ValidatorConfig& c = {}) {
    if (!s.completed || s.generation != ctx.currentGeneration ||
        s.sequence < confirmFloorSequence ||
        ctx.sampleAgeTicks > c.confirmMaxAgeTicks)
        return Verdict::Pending;
    return validate(s, ctx, c);
}

}  // namespace zonai_hookshot::pure
