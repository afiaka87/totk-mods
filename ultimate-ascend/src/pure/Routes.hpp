// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "DescendGeometry.hpp"

namespace descend::pure {

struct VoxelSurface {
    bool found = false;
    float y = NAN;
};

inline bool finitePoint(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

inline void setDestination(Plan& p, Vec3 origin, float underside) {
    p.underside = underside;
    p.thickness = p.floor - underside;
    p.target = at(origin, std::fmin(underside + tuning::kTargetAboveUnderside,
                                  std::fmin(origin.y, p.floor) - tuning::kTargetBelowStart));
    p.drop = at(origin, underside - tuning::kDropBelowUnderside);
}

// Vanilla's object-relative acceptance limits (queryValidPosition, 1.2.1).
inline bool acceptsMotion(Vec3 body, Vec3 player) {
    if (!finitePoint(body) || !finitePoint(player)) return false;
    const Vec3 d{body.x - player.x, body.y - player.y, body.z - player.z};
    const float speed2 = body.x * body.x + body.y * body.y + body.z * body.z;
    return speed2 <= 1.5f * 1.5f || d.x * d.x + d.y * d.y + d.z * d.z <= 5.25f * 5.25f;
}

// Uses the loaded voxel column, not missing distant collision, to nominate an exit. The event must still preload and validate physical collision before travel.
template <class Down, class Up>
Plan deepCandidate(Vec3 feet, const Plan& shallow, Down&& down, Up&& up) {
    Plan p{};
    p.route = Plan::Route::DeepTerrain;
    p.verdict = Verdict::UnverifiedExit;
    p.floor = shallow.floor;
    if (!finitePoint(feet) || !std::isfinite(p.floor) || shallow.entry != kEntryFaces ||
        shallow.verdict != Verdict::NoUnderside) return p;
    const auto ceiling = down(at(feet, p.floor - tuning::kInset), true);
    if (!ceiling.found || !std::isfinite(ceiling.y) || ceiling.y < -1500.0f ||
        p.floor - ceiling.y <= tuning::kMaxThickness) return p;
    setDestination(p, feet, ceiling.y);
    p.voxel = up(p.drop);
    if (!voxelConfirms(p.voxel, p.underside, p.floor)) return p;
    // Positive floor answers also establish that the lower column is available.
    const auto landing = down(at(feet, ceiling.y - tuning::kExitInset), false);
    if (!landing.found || !std::isfinite(landing.y) || ceiling.y - landing.y < tuning::kExitClearance + 1.0f)
        return p;
    p.landing = landing.y;
    for (int i = 0; i < tuning::kBodyDirections; ++i) {
        const auto d = viewOffset(i);
        const Vec3 q{feet.x + d.x * tuning::kBodyRadius, p.drop.y, feet.z + d.z * tuning::kBodyRadius};
        const auto v = up(q);
        const auto floor = down(q, false);
        if (!voxelConfirms(v, p.underside, p.floor) || !floor.found || !std::isfinite(floor.y) ||
            p.underside - floor.y < tuning::kExitClearance + 1.0f) return p;
    }
    p.verdict = Verdict::Valid;
    return p;
}

template <class Cast, class Up>
Plan loadedDeepExit(const Plan& candidate, Cast&& cast, Up&& up, bool views = true) {
    Plan p = candidate;
    p.verdict = Verdict::UnverifiedExit;
    if (p.route != Plan::Route::DeepTerrain || !finitePoint(p.drop)) return p;
    const auto bottom = cast(at(p.drop, p.underside - 2.0f), at(p.drop, p.underside + 2.0f),
                             kEntryFaces, Layer::Clipper);
    p.oneSideY = bottom.hit ? bottom.position.y : NAN;
    if (!bottom.hit || !finitePoint(bottom.position) ||
        bottom.position.y <= p.underside - 2.0f + tuning::kStartedInside) return p;
    setDestination(p, at(p.drop, p.floor), bottom.position.y);
    p = checkExit(p, at(p.drop, p.floor), NAN, cast, up, views);
    confirmExit(p);
    return p;
}

// Replay completed queries while extending the same deterministic survey by a bounded amount. Exit checks use hit positions, never normals. Camera views were collected during initial loading.
struct ExitSurvey {
    struct Hit { Vec3 position; bool hit; };
    static constexpr unsigned kCapacity = 5 + 2 * tuning::kRockCasts + 2 * tuning::kBodyCasts;
    Hit rays[kCapacity]{};
    VoxelUp voxels[2]{};
    unsigned count = 0, voxelCount = 0;
    unsigned index = 0, voxelIndex = 0, budget = 0;
    bool pending = false, overflow = false;

    void begin() { index = voxelIndex = 0; budget = 16; pending = overflow = false; }
    template<class Cast>
    CastResult ray(Vec3 a, Vec3 b, Faces faces, Layer layer, Cast&& cast) {
        if (pending) return {};
        if (index == kCapacity) { overflow = pending = true; return {}; }
        if (index == count) {
            if (!budget) { pending = true; return {}; }
            --budget;
            const auto hit = cast(a,b,faces,layer);
            rays[count++] = {hit.position,hit.hit};
        }
        const auto hit = rays[index++];
        return {hit.hit,hit.position,{}};
    }
    template<class Up>
    VoxelUp voxel(Vec3 q, Up&& up) {
        if (pending) return {};
        if (voxelIndex == 2) { overflow = pending = true; return {}; }
        if (voxelIndex == voxelCount) voxels[voxelCount++] = up(q);
        return voxels[voxelIndex++];
    }
    Plan finish(Plan result) const {
        if (pending) result.verdict = overflow ? Verdict::UnverifiedExit : Verdict::Pending;
        return result;
    }

    template<class Cast, class Up>
    Plan advance(const Plan& candidate, Cast&& cast, Up&& up) {
        begin();
        const auto cachedRay = [&](Vec3 a, Vec3 b, Faces faces, Layer layer) {
            return ray(a,b,faces,layer,cast);
        };
        const auto cachedVoxel = [&](Vec3 q) { return voxel(q,up); };
        return finish(loadedDeepExit(candidate,cachedRay,cachedVoxel,false));
    }
};

// Survey the whole exit footprint, as vanilla Ascend surveys a multi-body surface. Each column still needs an outside top/bottom pair; neighbouring pieces may differ.
template <class Cast, class Up, class BodyCast>
Plan objectPlan(Vec3 feet, const BodyCastResult& floor, Cast&& cast, Up&& up, BodyCast&& bodyCast, bool views = true) {
    Plan p{};
    p.route = Plan::Route::Object;
    p.verdict = Verdict::UnverifiedExit;
    if (!floor.hit || !floor.body || !finitePoint(floor.position)) return p;
    p.floor = floor.position.y;
    float underside = p.floor;
    for (int i = -1; i < tuning::kBodyDirections; ++i) {
        const Vec3 q = i < 0 ? feet : offsetBy(feet, viewOffset(i), tuning::kBodyRadius);
        const auto top = bodyCast(at(q, p.floor + tuning::kFloorAbove), at(q, p.floor - tuning::kBodyRadius));
        if (i < 0 && (!top.hit || !(top.body == floor.body) ||
            std::fabs(top.position.y - p.floor) > tuning::kSameSurface)) return p;
        if (!top.hit) continue; // Preserve small single objects and edges already accepted.
        if (!top.body || top.body.world != floor.body.world || !finitePoint(top.position)) return p;
        bool found = false;
        for (int step = 1; step <= static_cast<int>(tuning::kMaxThickness); ++step) {
            const auto start = at(q, top.position.y - static_cast<float>(step));
            const auto bottom = bodyCast(start, at(q, top.position.y + tuning::kFloorAbove));
            if (!bottom.hit || bottom.position.y <= start.y + tuning::kStartedInside) continue;
            if (!(bottom.body == top.body) || !finitePoint(bottom.position)) return p;
            underside = std::fmin(underside, bottom.position.y);
            found = true;
            break;
        }
        if (!found) return p;
    }
    setDestination(p, feet, underside);
    if (p.thickness < tuning::kMinThickness || p.thickness > tuning::kMaxThickness) return p;
    p.body = floor.body;
    p = checkExit(p, feet, NAN, cast, up, views);
    confirmExit(p);
    return p;
}

struct Matrix34 {
    float m[3][4]{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}};
    Vec3 point(Vec3 p) const {
        return {m[0][0]*p.x + m[0][1]*p.y + m[0][2]*p.z + m[0][3],
                m[1][0]*p.x + m[1][1]*p.y + m[1][2]*p.z + m[1][3],
                m[2][0]*p.x + m[2][1]*p.y + m[2][2]*p.z + m[2][3]};
    }
    Vec3 local(Vec3 p) const {
        const Vec3 d{p.x-m[0][3],p.y-m[1][3],p.z-m[2][3]};
        return {m[0][0]*d.x+m[1][0]*d.y+m[2][0]*d.z,
                m[0][1]*d.x+m[1][1]*d.y+m[2][1]*d.z,
                m[0][2]*d.x+m[1][2]*d.y+m[2][2]*d.z};
    }
    bool valid() const {
        for (const auto& row : m) for (float f : row) if (!std::isfinite(f)) return false;
        for (int a=0;a<3;++a) for (int b=0;b<3;++b) {
            const float dot=m[a][0]*m[b][0]+m[a][1]*m[b][1]+m[a][2]*m[b][2];
            if (std::fabs(dot-(a==b?1.0f:0.0f)) > 0.01f) return false;
        }
        return true;
    }
};

} // namespace descend::pure
