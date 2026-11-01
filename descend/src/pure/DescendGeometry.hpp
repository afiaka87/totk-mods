// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>

namespace descend::pure {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct CastResult {
    bool hit = false;
    Vec3 position{};
    Vec3 normal{};
};

// The game's ray query can keep hits on one side of a surface only (query +0x84, 1 or 2, handed to the physics engine's ray flags). Boot 3 showed 1 keeps the faces a ray meets head-on (up-facing for a downward ray) on every outside floor; 2 kept the floor only where Link stood inside a hollow island.
enum class Faces : std::uint8_t { All = 0, OneSide = 1, OtherSide = 2 };
inline constexpr Faces kEntryFaces = Faces::OneSide;
inline constexpr Faces kExitFaces = Faces::OtherSide;

// The collision layers the planner asks: the ceiling-clipper layer Ascend finds ceilings on, and the solid-world layer other mods use for the ground under the player.
enum class Layer : std::uint8_t { Clipper, Solid };

// The game's voxel look upward from a point (Ascend's own exit check): the first solid run above it and where that run ends, in 1 m cells rounded outward. Heights are NaN where the game wrote none.
struct VoxelUp {
    enum class Answer : std::uint8_t { NoModule, Busy, NoUnit, Unit } answer = Answer::NoModule;
    bool found = false; // the game's own result
    float firstSolid = NAN, solidEnd = NAN, nextSolid = NAN;
};

enum class Verdict : std::uint8_t {
    Valid,
    NoFloor,
    NoFaceFilter,
    StandingInside,
    InsideSolid,
    NoUnderside,
    TooThin,
    BlockedExit,
    TightExit,
    InsideRock,
    UnverifiedExit
};

inline constexpr const char* verdictName(Verdict verdict) {
    switch (verdict) {
    case Verdict::Valid: return "valid";
    case Verdict::NoFloor: return "no-floor";
    case Verdict::NoFaceFilter: return "no-face-filter";
    case Verdict::StandingInside: return "standing-inside";
    case Verdict::InsideSolid: return "inside-solid";
    case Verdict::NoUnderside: return "no-underside";
    case Verdict::TooThin: return "too-thin";
    case Verdict::BlockedExit: return "blocked-exit";
    case Verdict::TightExit: return "tight-exit";
    case Verdict::InsideRock: return "inside-rock";
    case Verdict::UnverifiedExit: return "unverified-exit";
    }
    return "unknown";
}

// What found the rock for an `inside-rock` refusal.
enum class RockReason : std::uint8_t { None, DropVotes, Voxel, LandingStart, LandingFace, LandingVotes };

inline constexpr const char* rockReasonName(RockReason reason) {
    switch (reason) {
    case RockReason::None: return "none";
    case RockReason::DropVotes: return "drop-votes";
    case RockReason::Voxel: return "voxel";
    case RockReason::LandingStart: return "landing-start";
    case RockReason::LandingFace: return "landing-face";
    case RockReason::LandingVotes: return "landing-votes";
    }
    return "unknown";
}

// Which sides of the first solid-world surface below the underside the one-sided casts kept.
enum class LandingFace : std::uint8_t { None, Front, Back, Both };

inline constexpr const char* landingFaceName(LandingFace face) {
    switch (face) {
    case LandingFace::None: return "none";
    case LandingFace::Front: return "front";
    case LandingFace::Back: return "back";
    case LandingFace::Both: return "both";
    }
    return "unknown";
}

// What cleared a stray down-facing face deep below the underside.
enum class StrayKept : std::uint8_t { No, Voxel, Landing };

inline constexpr const char* strayKeptName(StrayKept kept) {
    switch (kept) {
    case StrayKept::No: return "no";
    case StrayKept::Voxel: return "voxel";
    case StrayKept::Landing: return "landing";
    }
    return "unknown";
}

namespace tuning {
inline constexpr float kFloorAbove = 0.3f;
inline constexpr float kFloorBelow = 1.5f;
// Each cast below a surface starts this far under it so it cannot re-hit that surface.
inline constexpr float kInset = 0.02f;
// The exit cast starts just under the underside, so ground the slab rests on is still seen.
inline constexpr float kExitInset = 0.01f;
// Two hits this close are the same surface (floor calibration).
inline constexpr float kSameSurface = 0.02f;
// A hit this close to its ray start is the physics engine's report of a ray starting inside a solid shape.
inline constexpr float kStartedInside = 1e-4f;
inline constexpr float kMinThickness = 0.1f;
// Changed from 20 m after boot 11, where valid slabs reached the limit and most refusals were `no-underside`; was 10 m after boot 3 (a hollow island's floor was 12.4 m thick) and 20 m before boot 1.
inline constexpr float kMaxThickness = 40.0f;
// The walk keeps counting this far below the deepest allowed underside, so a stray down-facing face under a candidate underside shows that Link would still be inside.
inline constexpr float kOpenBelow = 20.0f;
inline constexpr int kMaxCrossings = 32; // changed from 16 after boot 3, for the count below the underside
inline constexpr float kTargetAboveUnderside = 0.5f;
inline constexpr float kTargetBelowStart = 0.1f;
// Cancel restores collision 1 m before its target; 2.8 m keeps Link's capsule clear of the slab then.
inline constexpr float kDropBelowUnderside = 2.8f;
// Nothing may stand under the underside within kExitClearance, just past the drop point; the exit cast reaches kExitScan to log how much room there is.
inline constexpr float kExitClearance = 2.9f;
inline constexpr float kExitScan = 3.9f;
// Link's body must fit where collision returns, his feet about 2 m below the underside: lines out to kBodyRadius from his drop line at each depth below the underside, and straight down at that radius from kBodyTop to kBodyBottom. Added after boot 9, where the drop line alone passed beside a rock column and Link was caught in it. Changed after boot 11 from a 0.5 m radius at depths 0.3-2.7 m with edge lines down to the exit clearance (too strict).
inline constexpr float kBodyRadius = 0.4f;
inline constexpr float kBodyDepths[]{0.6f, 1.3f, 2.0f};
inline constexpr float kBodyTop = 0.6f, kBodyBottom = 2.0f;
inline constexpr int kBodyDirections = 8;
inline constexpr int kBodyCasts = kBodyDirections * (1 + static_cast<int>(sizeof(kBodyDepths) / sizeof(float)));
// Inside-rock check from kRockDepth below the underside: eight level lines and one straight down, kRockReach long. A line votes when it meets the inside of a solid (a face kept only by the exit side) before any outside face; that many votes means the drop point is inside rock that the line straight down passed through without a face.
inline constexpr float kRockDepth = 2.0f, kRockReach = 60.0f;
inline constexpr int kRockDirections = 9, kRockVotes = 5;
inline constexpr int kRockCasts = kRockDirections * 2;
// Landing check on the solid-world layer, from just under the underside down kLandingReach. A surface within kLandClearance of the underside would meet Link's feet when collision returns about 1.8 m below it (the ceiling-clipper exit check keeps kExitClearance because the cancel stage snaps its goal to ceiling-clipper ground within 1 m). A landing that only the exit side keeps, or kRockVotes votes from kLandingWaist above it, is inside rock; the game's voxel answer kLandingProbe above it is logged.
inline constexpr float kLandingReach = 200.0f, kLandClearance = 2.2f, kLandingWaist = 1.0f, kLandingProbe = 0.5f;
inline constexpr int kLandingCasts = 3 + kRockCasts;
// The voxel map confirms the slab when, from the drop point, its first solid lies within kVoxelMatch of the underside and that solid ends within kVoxelMatch of the floor.
inline constexpr float kVoxelMatch = 1.5f;
// The return target sits above the start so collision returns in open air, not inside the floor.
inline constexpr float kReturnLift = 1.05f;
inline constexpr float kMarkerAbove = 0.3f;
inline constexpr float kMarkerBelow = 1.5f;
inline constexpr float kCameraHeightBelow = 1.0f;
// Changed from 2.2 m after boot 4: the legs end about 1.1 m below the underside, so 2.2 m left them at the top edge.
inline constexpr float kLookBelow = 1.2f;
inline constexpr float kCameraDistance = 4.0f; // changed from 4.5 m after boot 4
// Side views are checked from the camera spot: clear lines to Link's legs, to a little past the camera, to his feet, to the point he drops to, to either side of his legs, and room above and beside the camera. Changed after boot 8, where a view cleared by the single line from the legs still could not see Link; the first two lines are that check.
inline constexpr float kViewFrom = 0.6f;
inline constexpr float kViewMargin = 0.5f;
inline constexpr float kViewFeet = 1.1f;
inline constexpr float kViewSideBelow = 0.8f;
inline constexpr float kViewSide = 0.35f;
inline constexpr float kCameraClearance = 0.6f;
inline constexpr int kViewDirections = 8;
inline constexpr int kViewLines = 9;
// Fallback view from inside the open space the exit check proved, looking up at the legs; used when no side view is open.
inline constexpr float kBelowDepth = 1.6f;
inline constexpr float kBelowOffset = 0.2f;
// While Link moves through the slab the camera tracks him from the preview's side. On the way down too since boot 12, where v0.12.0's camera held above the floor, cut below the slab early and showed only Link's feet at the top of the screen; v0.12.0 had replaced the tracking view after boot 11 on the guess that it showed the inside of thick rock.
inline constexpr float kTrackHeight = 1.2f;
inline constexpr float kTrackLook = 0.9f;
// Camera phase blend times and limits, in milliseconds (replaced per-frame blend steps after boot 5).
inline constexpr std::int64_t kSinkBlendMs = 300, kPreviewBlendMs = 180, kDropBlendMs = 200, kRiseBlendMs = 400,
                              kHandbackMs = 600;
// Handback starts this long after the cancel stage stops, or this long after leaving the preview at the latest.
inline constexpr std::int64_t kCancelQuietMs = 100, kLeaveLimitMs = 3000;
// An event whose stages stop updating hands the camera back.
inline constexpr std::int64_t kStaleMs = 500;
// Only a camera whose own view is this close to Link is driven by the event.
inline constexpr float kCameraRange = 60.0f;
// After A, Link leaves the preview at this speed and speeds up like a fall, in metres per second; the cancel stage moves him in metres per 30 fps frame and its own speed is 0.67.
inline constexpr float kDropStartSpeed = 1.0f, kDropGravity = 9.8f, kDropMaxStep = 0.67f, kStepsPerSecond = 30.0f;
// The cancel stage ends into a normal fall once collision is back and Link's feet are this far below the underside.
inline constexpr float kReleaseDepth = 1.5f;
// Descend aim view: a camera kAimDistance from Link's waist, starting kAimPitch above level behind him along the player's view and looking down at him and the marker at his feet, pulled toward his waist when something stands behind him. The right stick tilts it within kAimPitchMin..kAimPitchMax and turns it. Blends in and out over kAimBlendMs. Changed after boot 10 from a fixed spot 2.4 m back and 2.6 m up looking 0.4 m ahead of his feet, cast from his head: Link was not visible and the view could not be tilted. The starting pitch changed from 35Â° after boot 11, toward the top-down view Clay asked for.
inline constexpr float kRadiansPerDegree = 0.017453292f;
inline constexpr float kAimPivot = 0.8f, kAimDistance = 3.9f, kAimPitch = 55 * kRadiansPerDegree,
                       kAimPitchMin = 5 * kRadiansPerDegree, kAimPitchMax = 75 * kRadiansPerDegree;
inline constexpr float kAimMargin = 0.3f, kAimMinReach = 0.25f;
inline constexpr std::int64_t kAimBlendMs = 300;
} // namespace tuning

struct Plan {
    Verdict verdict = Verdict::NoFloor;
    Vec3 target{};
    float floor = NAN;
    float underside = NAN;
    Vec3 drop{};
    float thickness = NAN;
    // Floor check: where each one-sided cast hit (NaN on a miss), and the value that kept the floor.
    float oneSideY = NAN, otherSideY = NAN;
    Faces entry = Faces::All;
    int crossings = 0; // surfaces counted below the floor, including the check below the underside
    float clearBelow = NAN; // room under the underside up to kExitScan (NaN: none of it blocked)
    int rockVotes = -1;     // inside-rock votes at the drop point (-1: not checked)
    RockReason rockReason = RockReason::None;
    float solidFloor = NAN; // where the solid-world layer meets the floor cast (log only; NaN: a miss)
    VoxelUp voxel{};        // the game's voxel answer at the drop point
    float stray = NAN;      // a down-facing face past the thickness limit with nothing entered above it
    StrayKept strayKept = StrayKept::No;
    float landing = NAN; // first solid-world surface below the underside (NaN: none within kLandingReach)
    LandingFace landingFace = LandingFace::None;
    int landingVotes = -1;     // inside-rock votes above the landing (-1: not checked)
    VoxelUp landingVoxel{};    // the game's voxel answer just above the landing (log only)
    int bodyLine = -1;         // the body line that met something: direction * 4 + line, 3 being the edge line
    std::uint8_t openViews = 0; // bit i: side view i sees Link from its camera spot
    std::uint8_t legsViews = 0; // bit i: side view i passes the boot 4 check alone (logged for comparison)

    [[nodiscard]] bool valid() const { return verdict == Verdict::Valid; }
};

inline constexpr Vec3 at(Vec3 p, float y) { return {p.x, y, p.z}; }

// Horizontal unit offset of side view i, counted in eighths of a turn from +Z.
inline Vec3 viewOffset(int i) {
    const float angle = static_cast<float>(i) * 0.785398163f;
    return {std::sin(angle), 0, std::cos(angle)};
}

inline Vec3 offsetBy(Vec3 p, Vec3 direction, float distance) {
    return {p.x + direction.x * distance, p.y + direction.y * distance, p.z + direction.z * distance};
}

// How many of side view i's lines, cast from its camera spot in order, are clear before the first blocked one.
template <class Cast>
int clearViewLines(Vec3 p, float underside, int view, Cast& cast) {
    using namespace tuning;
    const Vec3 out = viewOffset(view);
    const Vec3 side{out.z, 0, -out.x};
    const Vec3 camera{p.x + out.x * kCameraDistance, underside - kCameraHeightBelow, p.z + out.z * kCameraDistance};
    const Vec3 legs = at(p, underside - kViewSideBelow);
    const Vec3 ends[kViewLines]{
        at(p, underside - kViewFrom),
        offsetBy(camera, out, kViewMargin),
        at(p, underside - kViewFeet),
        at(p, underside - kDropBelowUnderside),
        offsetBy(camera, {0, 1, 0}, kCameraClearance),
        offsetBy(camera, side, kCameraClearance),
        offsetBy(camera, side, -kCameraClearance),
        offsetBy(legs, side, kViewSide),
        offsetBy(legs, side, -kViewSide),
    };
    int clear = 0;
    while (clear < kViewLines && !cast(camera, ends[clear], Faces::All, Layer::Clipper).hit &&
           !cast(camera, ends[clear], Faces::All, Layer::Solid).hit) ++clear;
    return clear;
}

// The first line on which something solid comes within Link's body radius of his drop line below the underside, as direction * 4 + line (lines 0-2 level at each depth, 3 the edge line down), or -1 when all are clear. The drop line itself is already proven open, so each line starts in open air.
template <class Cast>
int bodyBlocked(Vec3 p, float underside, Cast& cast) {
    using namespace tuning;
    const auto blocked = [&](Vec3 from, Vec3 to) {
        return cast(from, to, Faces::All, Layer::Clipper).hit ||
               cast(from, to, Faces::All, Layer::Solid).hit;
    };
    for (int i = 0; i < kBodyDirections; ++i) {
        const Vec3 out = viewOffset(i);
        int line = 0;
        for (const float depth : kBodyDepths) {
            const Vec3 from = at(p, underside - depth);
            if (blocked(from, offsetBy(from, out, kBodyRadius))) return i * 4 + line;
            ++line;
        }
        const Vec3 edge = offsetBy(at(p, underside - kBodyTop), out, kBodyRadius);
        if (blocked(edge, at(edge, underside - kBodyBottom))) return i * 4 + line;
    }
    return -1;
}

inline float distance(Vec3 a, Vec3 b) {
    const float x = b.x - a.x, y = b.y - a.y, z = b.z - a.z;
    return std::sqrt(x * x + y * y + z * z);
}

// Inside-rock votes from point q: each of the level lines and the line straight down that meets a face kept only by the exit side before any face kept by the entry side. Shapes reported to both sides (box colliders) do not vote.
template <class Cast>
int rockVotes(Vec3 q, Cast& cast, Layer layer) {
    using namespace tuning;
    int votes = 0;
    for (int i = 0; i < kRockDirections; ++i) {
        const Vec3 direction = i < kViewDirections ? viewOffset(i) : Vec3{0, -1, 0};
        const Vec3 end = offsetBy(q, direction, kRockReach);
        const CastResult front = cast(q, end, kEntryFaces, layer);
        const CastResult back = cast(q, end, kExitFaces, layer);
        if (!back.hit) continue;
        if (!front.hit || distance(q, back.position) < distance(q, front.position) - kSameSurface) ++votes;
    }
    return votes;
}

// True when the voxel map, looking up from the drop point, finds it in open air with the slab directly above.
inline bool voxelConfirms(const VoxelUp& voxel, float underside, float floor) {
    using namespace tuning;
    return voxel.answer == VoxelUp::Answer::Unit && voxel.found &&
           std::fabs(voxel.firstSolid - underside) <= kVoxelMatch && std::fabs(voxel.solidEnd - floor) <= kVoxelMatch;
}

// Collision faces alone can describe a hollow island's internal floor/ceiling as a valid opening (patch03). Require the native upward query to identify this slab from the proposed drop point before offering a target. Failure means unverified, never proof of solid rock; missing/busy data is retried by the next aiming update.
inline void confirmExit(Plan& plan) {
    if (plan.valid() && !voxelConfirms(plan.voxel, plan.underside, plan.floor))
        plan.verdict = Verdict::UnverifiedExit;
}

// Geometry-only planner fixtures may omit the native map. Production must also call confirmExit before arming.
struct NoVoxel {
    VoxelUp operator()(Vec3) const { return {}; }
};

// Plans a straight-down pass from feet position p; cast(from, to, faces, layer) is one world raycast and voxel(q) the game's voxel look upward from q. Rays hit surfaces from both sides and report the normal turned toward the ray, and the ground holds stacked surfaces a few centimetres apart, so neither normals nor surface order can find an underside. Instead the walk counts faces: a downward ray enters a solid through an up-facing face and leaves it through a down-facing one. The underside is where every solid entered below the floor has been left again. Some sky islands are hollow shells holding stray inner faces, so the count can reach zero inside one; the walk keeps counting below, and a down-facing face met with nothing entered means the candidate was still inside.
template <class Cast, class Voxel>
Plan plan(Vec3 p, Cast&& cast, Voxel&& voxel, bool collectViews = true) {
    using namespace tuning;
    Plan result{};
    const Vec3 top = at(p, p.y + kFloorAbove), bottom = at(p, p.y - kFloorBelow);
    const CastResult floor = cast(top, bottom, Faces::All, Layer::Clipper);
    if (!floor.hit) return result;
    const float floorY = floor.position.y;
    result.floor = floorY;

    // An outside floor is an entry face; a floor kept only by the exit side is a shell stood on from inside.
    const CastResult one = cast(top, bottom, Faces::OneSide, Layer::Clipper);
    const CastResult other = cast(top, bottom, Faces::OtherSide, Layer::Clipper);
    if (one.hit) result.oneSideY = one.position.y;
    if (other.hit) result.otherSideY = other.position.y;
    const bool oneOnFloor = one.hit && std::fabs(one.position.y - floorY) <= kSameSurface;
    const bool otherOnFloor = other.hit && std::fabs(other.position.y - floorY) <= kSameSurface;
    if (oneOnFloor == otherOnFloor) {
        result.verdict = Verdict::NoFaceFilter;
        return result;
    }
    result.entry = oneOnFloor ? Faces::OneSide : Faces::OtherSide;
    if (result.entry != kEntryFaces) {
        result.verdict = Verdict::StandingInside;
        return result;
    }
    // Whether the solid-world layer answers here at all (log only).
    const CastResult solidFloor = cast(top, bottom, Faces::All, Layer::Solid);
    if (solidFloor.hit) result.solidFloor = solidFloor.position.y;

    const float deepest = floorY - kMaxThickness;
    const float end = deepest - kOpenBelow;
    bool startedInside = false;
    const auto below = [&](float y, Faces faces) {
        const float from = y - kInset;
        const CastResult hit = cast(at(p, from), at(p, end), faces, Layer::Clipper);
        if (hit.hit && hit.position.y >= from - kStartedInside) startedInside = true;
        return hit;
    };
    CastResult in = below(floorY, kEntryFaces);
    CastResult out = below(floorY, kExitFaces);
    int depth = 1;
    float underside = NAN;
    float solidBelow = NAN; // first entry face below the current underside candidate
    while (!startedInside && (in.hit || out.hit)) {
        if (result.crossings == kMaxCrossings) {
            underside = NAN; // the count below the candidate is unfinished
            break;
        }
        ++result.crossings;
        if (in.hit && (!out.hit || in.position.y > out.position.y)) {
            if (depth++ == 0 && std::isnan(solidBelow)) solidBelow = in.position.y;
            in = below(in.position.y, kEntryFaces);
            continue;
        }
        if (depth > 1 || (depth == 1 && !std::isnan(underside))) {
            --depth;
        } else {
            // Leaves the last solid entered, or a solid the walk never saw entered: a new candidate. Past the thickness limit it ends the walk: without a candidate the slab is too thick, and below one it is a stray face the checks below may clear.
            if (out.position.y < deepest) {
                if (!std::isnan(underside)) result.stray = out.position.y;
                break;
            }
            depth = 0;
            underside = out.position.y;
            solidBelow = NAN;
        }
        out = below(out.position.y, kExitFaces);
    }
    if (startedInside) {
        result.verdict = Verdict::InsideSolid;
        return result;
    }
    if (std::isnan(underside)) {
        result.verdict = Verdict::NoUnderside;
        return result;
    }
    result.underside = underside;
    result.thickness = floorY - underside;
    if (result.thickness < kMinThickness) {
        result.verdict = Verdict::TooThin;
        return result;
    }
    const float start = std::fmin(p.y, floorY);
    result.target = at(p, std::fmin(underside + kTargetAboveUnderside, start - kTargetBelowStart));
    result.drop = at(p, underside - kDropBelowUnderside);
    // The first entry face below is the next solid; one that starts level with the underside is resting contact.
    const bool solidClose = !std::isnan(solidBelow) && solidBelow >= underside - kExitClearance;
    const CastResult exit = cast(at(p, underside - kExitInset), at(p, underside - kExitScan), Faces::All, Layer::Clipper);
    if (exit.hit) result.clearBelow = underside - exit.position.y;
    if (solidClose || (exit.hit && exit.position.y >= underside - kExitClearance)) {
        result.verdict = Verdict::BlockedExit;
        return result;
    }
    // This is an upward search, not an occupancy query. Failure can mean no ceiling or unavailable search data.
    result.voxel = voxel(result.drop);
    // Where Link lands on the solid-world layer, and whether that is the outside of a surface.
    const Vec3 landFrom = at(p, underside - kExitInset), landTo = at(p, underside - kExitInset - kLandingReach);
    const CastResult land = cast(landFrom, landTo, Faces::All, Layer::Solid);
    if (land.hit) {
        result.landing = land.position.y;
        if (land.position.y >= landFrom.y - kStartedInside) {
            result.rockReason = RockReason::LandingStart;
            result.verdict = Verdict::InsideRock;
            return result;
        }
        if (land.position.y >= underside - kLandClearance) {
            result.verdict = Verdict::BlockedExit;
            return result;
        }
        const CastResult front = cast(landFrom, landTo, kEntryFaces, Layer::Solid);
        const CastResult back = cast(landFrom, landTo, kExitFaces, Layer::Solid);
        const bool frontHere = front.hit && std::fabs(front.position.y - land.position.y) <= kSameSurface;
        const bool backHere = back.hit && std::fabs(back.position.y - land.position.y) <= kSameSurface;
        result.landingFace = frontHere ? (backHere ? LandingFace::Both : LandingFace::Front)
                                       : (backHere ? LandingFace::Back : LandingFace::None);
    }
    // A stray face is cleared by the voxel map confirming the slab, or by an outside landing floor above it.
    if (!std::isnan(result.stray)) {
        if (voxelConfirms(result.voxel, underside, floorY)) {
            result.strayKept = StrayKept::Voxel;
        } else if (result.landingFace == LandingFace::Front && result.landing > result.stray + kSameSurface) {
            result.strayKept = StrayKept::Landing;
        } else {
            result.verdict = Verdict::NoUnderside;
            return result;
        }
    }
    if (result.landingFace == LandingFace::Back) {
        result.rockReason = RockReason::LandingFace;
        result.verdict = Verdict::InsideRock;
        return result;
    }
    result.rockVotes = rockVotes(at(p, underside - kRockDepth), cast, Layer::Clipper);
    if (result.rockVotes >= kRockVotes) {
        result.rockReason = RockReason::DropVotes;
        result.verdict = Verdict::InsideRock;
        return result;
    }
    if (land.hit) {
        result.landingVotes = rockVotes(at(p, result.landing + kLandingWaist), cast, Layer::Solid);
        result.landingVoxel = voxel(at(p, result.landing + kLandingProbe));
        if (result.landingVotes >= kRockVotes) {
            result.rockReason = RockReason::LandingVotes;
            result.verdict = Verdict::InsideRock;
            return result;
        }
    }
    result.bodyLine = bodyBlocked(p, underside, cast);
    if (result.bodyLine >= 0) {
        result.verdict = Verdict::TightExit;
        return result;
    }
    // A side view must see Link from the camera spot itself. With none open the camera looks up at his legs from the open column under him.
    for (int i = 0; collectViews && i < kViewDirections; ++i) {
        const int clear = clearViewLines(p, underside, i, cast);
        const auto bit = static_cast<std::uint8_t>(1u << i);
        if (clear >= 2) result.legsViews |= bit;
        if (clear == kViewLines) result.openViews |= bit;
    }
    result.verdict = Verdict::Valid;
    return result;
}

// Plans without the game's voxel map.
template <class Cast>
Plan plan(Vec3 p, Cast&& cast) {
    return plan(p, cast, NoVoxel{});
}

struct Pose {
    Vec3 position{};
    Vec3 lookAt{};
};

// Horizontal viewing direction of a pose (camera toward look-at), or +Z when the pose looks straight up or down.
inline Vec3 horizontalDirection(const Pose& pose) {
    const float x = pose.lookAt.x - pose.position.x;
    const float z = pose.lookAt.z - pose.position.z;
    const float length = std::sqrt(x * x + z * z);
    if (!(length > 1e-3f)) return {0, 0, 1};
    return {x / length, 0, z / length};
}

// False for a camera elsewhere in the world, or one whose position is not a number.
inline bool cameraInRange(Vec3 camera, Vec3 link) {
    const float x = camera.x - link.x, y = camera.y - link.y, z = camera.z - link.z;
    return x * x + y * y + z * z <= tuning::kCameraRange * tuning::kCameraRange;
}

// The open side view nearest the preferred camera offset, or -1 when none is open.
inline int chooseView(std::uint8_t openViews, Vec3 preferred) {
    int best = -1;
    float bestDot = -2;
    for (int i = 0; i < tuning::kViewDirections; ++i) {
        if (!(openViews & (1u << i))) continue;
        const Vec3 offset = viewOffset(i);
        const float dot = offset.x * preferred.x + offset.z * preferred.z;
        if (dot > bestDot) {
            bestDot = dot;
            best = i;
        }
    }
    return best;
}

// Underside view: the camera hangs below the slab on an open side and looks at Link's legs and the space below. Changed after boot 4, where it sat 4.5 m back along the player's view whatever was there and walls hid the legs.
inline Pose previewPose(Vec3 link, float underside, int view, Vec3 preferred) {
    using namespace tuning;
    if (view < 0) {
        // Inside the column the exit check proved open, looking up at the legs.
        return {{link.x + preferred.x * kBelowOffset, underside - kBelowDepth, link.z + preferred.z * kBelowOffset},
                {link.x, underside - kLookBelow, link.z}};
    }
    const Vec3 offset = viewOffset(view);
    return {{link.x + offset.x * kCameraDistance, underside - kCameraHeightBelow, link.z + offset.z * kCameraDistance},
            {link.x, underside - kLookBelow, link.z}};
}

// Camera tuning, independent of the native aim camera. Full stick turns 90 degrees/second.
struct PreviewOrbit {
    float yaw = 0, pitch = 0, radius = tuning::kCameraDistance;
    void begin(const Pose& pose) {
        const auto delta = Vec3{pose.position.x - pose.lookAt.x, pose.position.y - pose.lookAt.y,
                                pose.position.z - pose.lookAt.z};
        radius = distance(pose.position, pose.lookAt);
        yaw = std::atan2(delta.x, delta.z);
        pitch = std::atan2(delta.y, std::sqrt(delta.x * delta.x + delta.z * delta.z));
    }
    void move(float x, float y, float seconds) {
        const auto axis = [](float value) {
            const float magnitude = std::fabs(value);
            return magnitude <= 0.15f ? 0.0f : std::copysign(std::fmin(1.0f, (magnitude - 0.15f) / 0.85f), value);
        };
        seconds = std::fmin(0.05f, std::fmax(0.0f, seconds));
        yaw -= axis(x) * 90 * tuning::kRadiansPerDegree * seconds;
        pitch = std::fmin(10 * tuning::kRadiansPerDegree,
                         std::fmax(-75 * tuning::kRadiansPerDegree,
                                   pitch - axis(y) * 60 * tuning::kRadiansPerDegree * seconds));
    }
    [[nodiscard]] Pose pose(Vec3 target, float underside) const {
        const Vec3 look = at(target, underside - tuning::kLookBelow);
        const float horizontal = radius * std::cos(pitch);
        return {{look.x + std::sin(yaw) * horizontal,
                 std::fmin(underside - 0.35f, look.y + radius * std::sin(pitch)),
                 look.z + std::cos(yaw) * horizontal}, look};
    }
};

// Pull the real camera toward the clear centre, checking both layers and a small camera envelope.
template <class Cast>
std::optional<Pose> clearPreviewPose(Pose wanted, Cast&& cast) {
    constexpr float margin = 0.15f;
    const Vec3 delta{wanted.position.x - wanted.lookAt.x, wanted.position.y - wanted.lookAt.y,
                     wanted.position.z - wanted.lookAt.z};
    const float length = distance(wanted.position, wanted.lookAt);
    if (!(length > 0.01f)) return std::nullopt;
    float reach = length;
    for (const auto layer : {Layer::Clipper, Layer::Solid}) {
        const auto hit = cast(wanted.lookAt, wanted.position, Faces::All, layer);
        if (hit.hit) reach = std::fmin(reach, std::fmax(0.0f, distance(wanted.lookAt, hit.position) - margin));
    }
    const Vec3 probes[]{{margin, 0, 0}, {-margin, 0, 0}, {0, margin, 0}, {0, -margin, 0},
                        {0, 0, margin}, {0, 0, -margin}};
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (reach < 0.05f) return std::nullopt;
        wanted.position = offsetBy(wanted.lookAt, delta, reach / length);
        bool blocked = false;
        for (const auto probe : probes) {
            const Vec3 end = offsetBy(wanted.position, probe, 1);
            if (cast(wanted.position, end, Faces::All, Layer::Clipper).hit ||
                cast(wanted.position, end, Faces::All, Layer::Solid).hit) { blocked = true; break; }
        }
        if (!blocked) return wanted;
        reach *= 0.5f;
    }
    return std::nullopt;
}

inline float smoothstep(float t) {
    t = std::fmin(std::fmax(t, 0.0f), 1.0f);
    return t * t * (3 - 2 * t);
}

inline Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// Eased blend from one pose to another; weight 0 is `from`, 1 is `to`.
inline Pose blend(const Pose& from, const Pose& to, float weight) {
    const float t = smoothstep(weight);
    return {lerp(from.position, to.position, t), lerp(from.lookAt, to.lookAt, t)};
}

// Pitch of a pose's view above level, in radians (negative when it looks down).
inline float pitchUp(const Pose& pose) {
    const float x = pose.lookAt.x - pose.position.x, y = pose.lookAt.y - pose.position.y,
                z = pose.lookAt.z - pose.position.z;
    return std::atan2(y, std::sqrt(x * x + z * z));
}

// The aim view's pitch above level: its starting pitch less however far the game's own aim camera has tilted up since Descend aim began, so the right stick tilts it the way the player's camera settings tilt the game's view.
inline float aimPitch(float nativePitch, float nativeStart) {
    using namespace tuning;
    return std::fmin(kAimPitchMax, std::fmax(kAimPitchMin, kAimPitch - (nativePitch - nativeStart)));
}

// The Descend aim view looks at Link's waist from a spot `pitch` above level behind him along the player's view.
inline Vec3 aimPivot(Vec3 link) { return at(link, link.y + tuning::kAimPivot); }

inline Vec3 aimSpot(Vec3 link, Vec3 direction, float pitch) {
    using namespace tuning;
    const Vec3 pivot = aimPivot(link);
    const float back = kAimDistance * std::cos(pitch), up = kAimDistance * std::sin(pitch);
    return {pivot.x - direction.x * back, pivot.y + up, pivot.z - direction.z * back};
}

// The part of the waist-to-spot line the aim camera may use: all of it on a clear cast, otherwise up to kAimMargin short of what the cast hit, and never less than kAimMinReach.
inline float aimReach(Vec3 pivot, Vec3 spot, const CastResult& hit) {
    using namespace tuning;
    if (!hit.hit) return 1;
    const float dx = spot.x - pivot.x, dy = spot.y - pivot.y, dz = spot.z - pivot.z;
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(length > 1e-3f)) return kAimMinReach;
    const float hx = hit.position.x - pivot.x, hy = hit.position.y - pivot.y, hz = hit.position.z - pivot.z;
    const float reach = (std::sqrt(hx * hx + hy * hy + hz * hz) - kAimMargin) / length;
    return std::fmin(1.0f, std::fmax(kAimMinReach, reach));
}

// Descend aim view: the camera at `reach` along the waist-to-spot line, looking at Link's waist.
inline Pose aimPose(Vec3 link, Vec3 direction, float pitch, float reach) {
    return {lerp(aimPivot(link), aimSpot(link, direction, pitch), reach), aimPivot(link)};
}

// After B the cancel stage carries Link up to kReturnLift above his start and gives collision back about a metre short of that; it ends there once his feet are back above the start, so he lands on the floor instead of dropping about a metre.
inline bool returnReleases(bool collision, float link, float start) { return collision && link >= start; }

// Weight of the aim view: it rises to 1 over kAimBlendMs while the view is on and falls back to 0 while it is off, starting from wherever it was when it last turned.
struct AimBlend {
    bool on = false;
    float from = 0;
    std::int64_t since = 0;

    [[nodiscard]] float weight(std::int64_t ms) const {
        const float moved = static_cast<float>(ms - since) / static_cast<float>(tuning::kAimBlendMs);
        return std::fmin(1.0f, std::fmax(0.0f, on ? from + moved : from - moved));
    }
    // True when the view turned on or off.
    bool turn(bool next, std::int64_t ms) {
        if (next == on) return false;
        from = weight(ms);
        since = ms;
        on = next;
        return true;
    }
};

// Up direction without roll: world up less its part along the view. False when the view is within about 3Â° of vertical or not a number. Added after boot 7, where the game camera's own up, leaning with its downward pitch, rolled the preview view.
inline bool levelUp(const Pose& pose, Vec3& up) {
    float x = pose.lookAt.x - pose.position.x, y = pose.lookAt.y - pose.position.y, z = pose.lookAt.z - pose.position.z;
    const float length = std::sqrt(x * x + y * y + z * z);
    if (!(length > 1e-3f)) return false;
    x /= length;
    y /= length;
    z /= length;
    const float horizontal = std::sqrt(1 - y * y);
    if (!(horizontal > 0.05f)) return false;
    up = {-x * y / horizontal, horizontal, -z * y / horizontal};
    return true;
}

// The cancel stage's speed this long after the fall began, in metres per 30 fps frame.
inline float dropStep(float seconds) {
    using namespace tuning;
    return std::fmin((kDropStartSpeed + kDropGravity * std::fmax(seconds, 0.0f)) / kStepsPerSecond, kDropMaxStep);
}

// Vertical tracking view while Link moves through the slab, from the side the preview used (on the way down too, except in v0.12.0).
inline Pose trackPose(Vec3 link, int view, Vec3 preferred) {
    using namespace tuning;
    const Vec3 offset = view < 0 ? preferred : viewOffset(view);
    return {{link.x + offset.x * kCameraDistance, link.y + kTrackHeight, link.z + offset.z * kCameraDistance},
            {link.x, link.y + kTrackLook, link.z}};
}

// After A the camera stays at the preview spot and watches Link leave the slab; while he is still above the preview look-at it keeps that look-at, so the view does not jump.
inline Pose dropPose(Vec3 link, Vec3 target, float underside, int view, Vec3 preferred) {
    using namespace tuning;
    const Pose preview = previewPose(target, underside, view, preferred);
    return {preview.position, {link.x, std::fmin(preview.lookAt.y, link.y + kTrackLook), link.z}};
}

enum class Phase : std::uint8_t { Off, Sink, Preview, Drop, Rise, Handback };

inline constexpr const char* phaseName(Phase phase) {
    switch (phase) {
    case Phase::Off: return "off";
    case Phase::Sink: return "sink";
    case Phase::Preview: return "preview";
    case Phase::Drop: return "drop";
    case Phase::Rise: return "rise";
    case Phase::Handback: return "handback";
    }
    return "unknown";
}

// Event times in milliseconds on one clock; zero means it has not happened.
struct Timeline {
    std::int64_t start = 0;   // event owned
    std::int64_t preview = 0; // first preview update
    std::int64_t leave = 0;   // preview left, or the cancel stage started without one
    std::int64_t cancel = 0;  // last cancel-stage update
    std::int64_t touched = 0; // last update of any owned stage
    bool drop = false;        // left with A
};

// Camera phase at `now`. Leaving the preview drops or rises until the cancel stage stops (or a time limit), then the camera is handed back to the game over kHandbackMs.
inline Phase phaseAt(const Timeline& t, std::int64_t now) {
    using namespace tuning;
    if (!t.start) return Phase::Off;
    std::int64_t handback;
    if (t.leave) {
        if (t.cancel && now - t.cancel > kCancelQuietMs)
            handback = t.cancel + kCancelQuietMs;
        else if (now - t.leave > kLeaveLimitMs)
            handback = t.leave + kLeaveLimitMs;
        else
            return t.drop ? Phase::Drop : Phase::Rise;
    } else if (now - t.touched > kStaleMs) {
        handback = t.touched + kStaleMs;
    } else {
        return t.preview ? Phase::Preview : Phase::Sink;
    }
    return now - handback > kHandbackMs ? Phase::Off : Phase::Handback;
}

inline std::int64_t blendMs(Phase phase) {
    using namespace tuning;
    switch (phase) {
    case Phase::Sink: return kSinkBlendMs;
    case Phase::Preview: return kPreviewBlendMs;
    case Phase::Drop: return kDropBlendMs;
    case Phase::Rise: return kRiseBlendMs;
    default: return kHandbackMs;
    }
}

struct CameraScene {
    Vec3 link{};
    Vec3 target{};
    float underside = 0;
    int view = -1;
    Vec3 preferred{};
    Pose native{};
};

// The pose a phase settles on; Off and Handback settle on the game's own camera.
inline Pose phasePose(Phase phase, const CameraScene& s) {
    switch (phase) {
    case Phase::Sink:
    case Phase::Rise: return trackPose(s.link, s.view, s.preferred);
    case Phase::Preview: return previewPose(s.target, s.underside, s.view, s.preferred);
    case Phase::Drop: return dropPose(s.link, s.target, s.underside, s.view, s.preferred);
    default: return s.native;
    }
}

// Descend mode toggles on a fresh click of the stick while Ascend is aiming and clears once aiming ends.
struct ModeSwitch {
    bool descend = false;
    bool held = false;

    // Returns true when this sample changed the mode.
    bool update(bool aiming, bool aimingLapsed, bool clickHeld) {
        const bool pressed = clickHeld && !held;
        held = clickHeld;
        if (aimingLapsed) {
            const bool changed = descend;
            descend = false;
            return changed;
        }
        if (aiming && pressed) {
            descend = !descend;
            return true;
        }
        return false;
    }
};

inline bool sameBits(Vec3 a, Vec3 b) {
    return std::memcmp(&a, &b, sizeof(Vec3)) == 0;
}

} // namespace descend::pure
