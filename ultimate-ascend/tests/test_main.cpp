// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "DescendGeometry.hpp"
#include "Proximity.hpp"
#include "Routes.hpp"
#include "MarkerScale.hpp"
#include "../patch/Decode.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using namespace descend::pure;

TEST_CASE("Distant upward markers grow gradually and stay bounded") {
    for (const float h : {-1000.0f, 0.0f, 100.0f, 300.0f, NAN, INFINITY}) CHECK(upwardMarkerScale(h) == 1.0f);
    CHECK(upwardMarkerScale(300.6f) == doctest::Approx(1.001f));
    CHECK(upwardMarkerScale(900.0f) == 2.0f);
    CHECK(upwardMarkerScale(1500.0f) == 3.0f);
    CHECK(upwardMarkerScale(9999.0f) == 3.0f);
}

TEST_CASE("Compressed initialization rejects invalid offsets and output overruns") {
    struct Reader {
        std::vector<std::uint8_t> bytes;
        std::size_t index=0;
        std::size_t remaining() const { return bytes.size()-index; }
        bool operator()(std::uint8_t& byte) {
            if (!remaining()) return false;
            byte=bytes[index++]; return true;
        }
    };
    const auto run=[](std::vector<std::uint8_t> bytes,std::size_t size,bool expected) {
        Reader reader{std::move(bytes)};
        std::vector<std::uint8_t> out(size+8,0xCD);
        CHECK(decodeBlock(reader,out.data(),size)==expected);
        CHECK(std::all_of(out.begin()+size,out.end(),[](auto b){return b==0xCD;}));
        return out;
    };
    CHECK(run({0x30,'a','b','c'},3,true)[2]=='c');
    // One literal followed by an overlapping match and a final literal sequence.
    auto repeated=run({0x11,'a',1,0,0x50,'a','a','a','a','a'},11,true);
    CHECK(std::all_of(repeated.begin(),repeated.begin()+11,[](auto b){return b=='a';}));
    run({0x30,'a'},3,false);
    run({0x30,'a','b','c'},2,false);
    run({0x10,'a',0,0},5,false);
    run({0x10,'a',2,0},5,false);
    run({0x1F,'a',1,0,255},20,false);
    run({0xF0,255,255,255},20,false);
    run({0x10,'a',1},5,false);
    run({},1,false);
}

TEST_CASE("Nearby hints expire and never become full green") {
    Proximity hint;
    const Vec3 here{0, 10, 0};
    hint.begin(1, here, 100);
    CHECK(hint.warmth(here) == 0);
    hint.observe(here, {4,10,0}, true, false, 100);
    CHECK(hint.warmth(here) > 0);
    CHECK(hint.warmth({3,10,0}) > hint.warmth(here));
    CHECK(hint.warmth({4,10,0}) == doctest::Approx(0.8f));
    hint.begin(1, here, 700);
    CHECK_FALSE(hint.known); // aim lapse
    hint.observe(here, here, true, false, 700);
    hint.begin(2, here, 701);
    CHECK_FALSE(hint.known); // new live actor
    hint.observe(here, here, true, false, 701);
    hint.begin(2, {0,12,0}, 702);
    CHECK_FALSE(hint.known); // another floor
}

TEST_CASE("Nearby search is paced and refresh failure removes a hint") {
    Proximity hint;
    const Vec3 here{0,10,0};
    hint.begin(1, here, 100);
    bool refresh = false;
    const auto first = hint.next(here, 100, refresh);
    CHECK_FALSE(refresh);
    CHECK(Proximity::distance(first, here) == doctest::Approx(1.5f));
    CHECK_FALSE(hint.due(199));
    CHECK(hint.due(200));
    hint.observe(here, first, true, false, 100);
    const auto second = hint.next(here, 500, refresh);
    CHECK(refresh);
    CHECK(second.z == first.z);
    hint.observe(here, second, false, true, 500);
    CHECK_FALSE(hint.known);
    hint.observe(here, {1,12,0}, true, false, 500);
    CHECK_FALSE(hint.known);
    hint.observe(here, {4,10,0}, true, false, 500);
    hint.observe(here, {1,10,0}, true, false, 501);
    CHECK(hint.spot.x == 1);
}

namespace {

// One horizontal mesh surface; up is its geometric facing. Plain ground is a single up-facing surface. Shapes with clipper unset are only on the solid-world layer, like the floor Link landed on in boot 12.
struct Surface {
    float y;
    bool up;
    bool clipper = true;
    float slopeX = 0; // rise per metre from kFeetX
};

// A solid convex shape between two heights (a box collider such as a wooden deck).
struct Box {
    float bottom, top;
    bool clipper = true;
    BodyKey body{};
    float minX = -INFINITY, maxX = INFINITY, minZ = -INFINITY, maxZ = INFINITY;
};

constexpr float kFeetX = 10, kFeetZ = -5;

// A vertical wall on the plane x = at (acrossX) or z = at, between two heights; hit from either side.
struct Wall {
    bool acrossX;
    float at, bottom, top;
};

// A horizontal shelf that only reaches across x from minX outward (a ledge or the foot of a rock column); hit from either side by rays that cross its height.
struct Ledge {
    float y, minX;
};

// A rock mass made of mesh faces between two corners: a ray entering it meets an outside face, one leaving it the inside of a face. Without a top face, as in the rock under the boot 11 branch, a ray from above enters unseen. Without sides its faces are reported to both side values, like a box collider.
struct Rock {
    Vec3 low, high;
    bool top = true;
    bool sides = true;
    bool clipper = true;
};

// Rays over infinite horizontal surfaces, behaving as boots 1 and 2 measured: mesh surfaces are hit from either side and the reported normal faces the ray. A one-sided query keeps only faces whose normal opposes the ray (front) or only the others (back); frontValue says which raw value keeps front faces. Walls only matter to the slanted view casts. Both layers hold every shape except those marked off the ceiling-clipper layer; the solid-world layer can be silent or lack the face filter, as neither was boot-tested before v0.13.0.
struct World {
    std::vector<Surface> surfaces;
    std::vector<Box> boxes;
    std::vector<Wall> walls;
    std::vector<Ledge> ledges;
    std::vector<Rock> rocks;
    bool filterWorks = true;
    Faces frontValue = Faces::OneSide;
    bool boxesIgnoreFilter = true; // the engine's side flags name triangles
    bool insideHits = true;        // a ray starting inside a box reports a hit at its start
    bool solidAnswers = true;
    bool solidFilter = true;
    int casts = 0;

    CastResult operator()(Vec3 from, Vec3 to, Faces faces, Layer layer, BodyKey* body = nullptr) {
        ++casts;
        const bool clipper = layer == Layer::Clipper;
        if (!clipper && !solidAnswers) return {};
        const bool filters = filterWorks && (clipper || solidFilter);
        const float low = std::min(from.y, to.y), high = std::max(from.y, to.y);
        const float direction = to.y < from.y ? -1.0f : 1.0f;
        CastResult best{};
        float bestT = INFINITY;
        const auto keep = [&](float t, Vec3 normal) {
            if (t >= bestT) return;
            bestT = t;
            best = {true, lerpTo(from, to, t), normal};
            if (body) *body = {};
        };
        const auto consider = [&](float y, bool up, bool filtered) {
            if (y < low || y > high) return;
            if (filtered && faces != Faces::All) {
                const bool front = (up ? 1.0f : -1.0f) * direction < 0;
                if (front != (faces == frontValue)) return;
            }
            if (to.y == from.y) {
                if (y == from.y) keep(0, {0, -direction, 0});
                return;
            }
            keep((y - from.y) / (to.y - from.y), {0, -direction, 0});
        };
        for (const auto& surface : surfaces) {
            if (!surface.clipper && clipper) continue;
            if (surface.slopeX == 0) { consider(surface.y, surface.up, filters); continue; }
            const float start = from.y - surface.y - surface.slopeX * (from.x - kFeetX);
            const float finish = to.y - surface.y - surface.slopeX * (to.x - kFeetX);
            if (start == finish) continue;
            const float t = start / (start - finish);
            if (t < 0 || t > 1) continue;
            const bool front = surface.up ? finish < start : finish > start;
            if (filters && faces != Faces::All && front != (faces == frontValue)) continue;
            const float sign = finish < start ? 1.0f : -1.0f;
            keep(t, {-surface.slopeX * sign, sign, 0});
        }
        for (const auto& box : boxes) {
            if (!box.clipper && clipper) continue;
            const auto previous = bestT;
            if (std::isfinite(box.minX)) {
                const bool inside = from.x > box.minX && from.x < box.maxX &&
                    from.z > box.minZ && from.z < box.maxZ && from.y > box.bottom && from.y < box.top;
                if (inside && insideHits) keep(0, {0,0,0});
                else considerRock({{box.minX,box.bottom,box.minZ},{box.maxX,box.top,box.maxZ},
                                   true,!boxesIgnoreFilter,box.clipper}, from,to,faces,filters,keep);
                if (body && bestT != previous) *body = box.body;
                continue;
            }
            if (from.y > box.bottom && from.y < box.top) {
                if (insideHits) consider(from.y, true, false);
                if (body && bestT != previous) *body = box.body;
                continue;
            }
            consider(box.top, true, filters && !boxesIgnoreFilter);
            consider(box.bottom, false, filters && !boxesIgnoreFilter);
            if (body && bestT != previous) *body = box.body;
        }
        for (const auto& wall : walls) {
            const float a = wall.acrossX ? from.x : from.z, b = wall.acrossX ? to.x : to.z;
            if ((a - wall.at) * (b - wall.at) >= 0) continue;
            const float t = (wall.at - a) / (b - a);
            const float y = from.y + (to.y - from.y) * t;
            if (y >= wall.bottom && y <= wall.top) keep(t, {0, 0, 0});
        }
        for (const auto& ledge : ledges) {
            if (to.y == from.y || (from.y - ledge.y) * (to.y - ledge.y) > 0) continue;
            const float t = (ledge.y - from.y) / (to.y - from.y);
            if (from.x + (to.x - from.x) * t >= ledge.minX) keep(t, {0, -direction, 0});
        }
        for (const auto& rock : rocks)
            if (rock.clipper || !clipper) considerRock(rock, from, to, faces, filters, keep);
        return best;
    }

    // Slab test over the rock's three axes; the axis that sets the entry or exit time names the face crossed.
    template <class Keep>
    void considerRock(const Rock& rock, Vec3 from, Vec3 to, Faces faces, bool filters, Keep& keep) const {
        const float a[3]{from.x, from.y, from.z}, b[3]{to.x, to.y, to.z};
        const float low[3]{rock.low.x, rock.low.y, rock.low.z}, high[3]{rock.high.x, rock.high.y, rock.high.z};
        float enter = -INFINITY, leave = INFINITY;
        int enterAxis = -1, leaveAxis = -1;
        for (int axis = 0; axis < 3; ++axis) {
            const float d = b[axis] - a[axis];
            if (d == 0) {
                if (a[axis] < low[axis] || a[axis] > high[axis]) return;
                continue;
            }
            float t0 = (low[axis] - a[axis]) / d, t1 = (high[axis] - a[axis]) / d;
            if (t0 > t1) std::swap(t0, t1);
            if (t0 > enter) enter = t0, enterAxis = axis;
            if (t1 < leave) leave = t1, leaveAxis = axis;
        }
        if (enter > leave) return;
        const bool down = b[1] < a[1];
        const auto face = [&](float t, int axis, bool entering) {
            if (t < 0 || t > 1) return;
            // Entering downward or leaving upward crosses the top face.
            if (!rock.top && axis == 1 && entering == down) return;
            if (filters && rock.sides && faces != Faces::All && entering != (faces == frontValue)) return;
            keep(t, {0, 0, 0});
        };
        face(enter, enterAxis, true);
        face(leave, leaveAxis, false);
    }

    static Vec3 lerpTo(Vec3 a, Vec3 b, float t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    }
};

// Walls around Link's column at the given horizontal distance, below the slab (none where open is set).
void boxIn(World& world, float distance, bool openMinusZ = false) {
    world.walls.push_back({true, kFeetX + distance, 60, 99});
    world.walls.push_back({true, kFeetX - distance, 60, 99});
    world.walls.push_back({false, kFeetZ + distance, 60, 99});
    if (!openMinusZ) world.walls.push_back({false, kFeetZ - distance, 60, 99});
}

// A closed slab: up-facing top, down-facing bottom.
void addSlab(World& world, float bottom, float top) {
    world.surfaces.push_back({top, true});
    world.surfaces.push_back({bottom, false});
}

constexpr Vec3 kFeet{kFeetX, 100.08f, kFeetZ}; // Link's feet sit a few centimetres above the floor, as in boot 1

} // namespace

TEST_CASE("Hint planning skips camera work but retains terrain and body rejection") {
    World world;
    addSlab(world, 99, 100);
    world.surfaces.push_back({40, true});
    const auto voxel = [](Vec3) { return VoxelUp{VoxelUp::Answer::Unit, true, 99, 100, 120}; };
    unsigned calls = 0;
    auto cast = [&](Vec3 from, Vec3 to, Faces faces, Layer layer) { ++calls; return world(from, to, faces, layer); };
    auto hint = plan(kFeet, cast, voxel, false);
    confirmExit(hint);
    REQUIRE(hint.valid());
    CHECK(calls <= 192);
    const unsigned hintCalls = calls;
    calls = 0;
    auto full = plan(kFeet, cast, voxel);
    confirmExit(full);
    CHECK(full.verdict == hint.verdict);
    CHECK(full.target.y == hint.target.y);
    CHECK(calls > hintCalls);
    CHECK(hint.openViews == 0);
    world.walls.push_back({true, kFeetX + 0.1f, 90, 99});
    CHECK(plan(kFeet, cast, voxel, false).verdict == Verdict::TightExit);
    world.walls.clear();
    hint = plan(kFeet, cast, NoVoxel{}, false);
    confirmExit(hint);
    CHECK(hint.verdict == Verdict::UnverifiedExit);
}

TEST_CASE("a thin floating slab plans a drop below its underside") {
    World world;
    addSlab(world, 99, 100);
    world.surfaces.push_back({40, true}); // ground far below
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    CHECK(result.floor == doctest::Approx(100));
    CHECK(result.underside == doctest::Approx(99));
    CHECK(result.thickness == doctest::Approx(1));
    CHECK(result.target.y == doctest::Approx(99.5));
    CHECK(result.target.x == kFeet.x);
    CHECK(result.target.z == kFeet.z);
    CHECK(result.drop.y == doctest::Approx(99 - 2.8));
    CHECK(result.entry == Faces::OneSide);
    CHECK(result.crossings == 2); // the ground 60 m down is within reach since boot 11
    CHECK(result.oneSideY == doctest::Approx(100));
    CHECK(result.otherSideY == doctest::Approx(99)); // the slab's bottom is within the floor cast's reach
    CHECK(result.openViews == 0xFF);
}

TEST_CASE("boot 11: an opening closed in on every side is accepted with the view from below") {
    World world;
    addSlab(world, 99, 100);
    world.surfaces.push_back({40, true});
    boxIn(world, 2);
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    CHECK(result.openViews == 0);
    CHECK(chooseView(result.openViews, {0, 0, 1}) == -1);
    World roomy;
    addSlab(roomy, 99, 100);
    boxIn(roomy, 6);
    CHECK(plan(kFeet, roomy).openViews == 0xFF);
}

TEST_CASE("boot 4: the underside camera takes the open side nearest the player's view") {
    World world;
    addSlab(world, 99, 100);
    boxIn(world, 2, true);
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    CHECK(result.openViews == (1u << 4)); // only straight toward -Z
    const Vec3 behind{0, 0, 1};           // the player looked toward -Z, so the camera would sit at +Z
    const int view = chooseView(result.openViews, behind);
    CHECK(view == 4);
    const auto pose = previewPose(result.target, result.underside, view, behind);
    CHECK(pose.position.x == doctest::Approx(kFeetX));
    CHECK(pose.position.z == doctest::Approx(kFeetZ - tuning::kCameraDistance));
    CHECK(pose.position.y == doctest::Approx(98));
    CHECK(pose.lookAt.y == doctest::Approx(99 - 1.2));
    CHECK(chooseView(0b101, {0.6f, 0, 0.8f}) == 0);
    CHECK(chooseView(0b101, {0.8f, 0, 0.6f}) == 2);
    CHECK(chooseView(0, behind) == -1);
}

TEST_CASE("boot 8: a side view must see Link's drop from the camera spot itself") {
    World world;
    addSlab(world, 99, 100);
    world.walls.push_back({false, kFeetZ - 3, 60, 97.8f}); // a low ridge between Link and the -Z camera spot
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    CHECK(result.legsViews == 0xFF); // the boot 4 line from the legs passes over the ridge
    CHECK(result.openViews == (0xFF & ~(1u << 4)));
}

TEST_CASE("boot 8: a camera spot hugging a wall is not an open view") {
    World world;
    addSlab(world, 99, 100);
    world.walls.push_back({true, kFeetX + 0.55f, 97.5f, 98.5f});
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    CHECK(result.legsViews == 0xF1); // +Z and -Z cameras see the legs past the wall's edge
    CHECK(result.openViews == 0xE0); // but the wall is beside those cameras
    World closed;
    addSlab(closed, 99, 100);
    closed.walls.push_back({true, kFeetX - 0.55f, 97.5f, 98.5f});
    closed.walls.push_back({true, kFeetX + 0.55f, 97.5f, 98.5f});
    const auto both = plan(kFeet, closed);
    CHECK(both.valid()); // refused as enclosed until boot 11
    CHECK(both.openViews == 0);
}

TEST_CASE("boot 9: rock beside the drop line within Link's body width is refused") {
    World column;
    addSlab(column, 99, 100);
    column.walls.push_back({true, kFeetX + 0.13f, 60, 98.9f});
    const auto result = plan(kFeet, column);
    CHECK(result.verdict == Verdict::TightExit);
    CHECK(result.underside == doctest::Approx(99));
    CHECK(result.bodyLine == 4);  // the level line at 0.6 m toward +x+z meets it first
    CHECK(result.openViews == 0); // refused before the views are checked
    World low;
    addSlab(low, 99, 100);
    low.walls.push_back({false, kFeetZ - 0.35f, 60, 97.1f});
    CHECK(plan(kFeet, low).verdict == Verdict::TightExit);
    World clear;
    addSlab(clear, 99, 100);
    clear.walls.push_back({true, kFeetX + 0.7f, 60, 98.9f});
    const auto open = plan(kFeet, clear);
    REQUIRE(open.valid());
    CHECK((open.openViews & 0xE0) == 0xE0); // the far side's views stay open
}

TEST_CASE("boot 9: a shelf under the slab that misses the drop line but meets Link's body is refused") {
    World shelf;
    addSlab(shelf, 99, 100);
    shelf.ledges.push_back({97.5f, kFeetX + 0.3f});
    CHECK(plan(kFeet, shelf).verdict == Verdict::TightExit);
    World wide;
    addSlab(wide, 99, 100);
    wide.ledges.push_back({97.5f, kFeetX + 0.6f});
    CHECK(plan(kFeet, wide).valid());
    World deep;
    addSlab(deep, 99, 100);
    deep.ledges.push_back({96.5f, kFeetX + 0.3f}); // below his feet when collision returns: he lands on it
    CHECK(plan(kFeet, deep).valid());
}

TEST_CASE("boot 11: a drop point inside rock that the line straight down passed through is refused") {
    World world;
    addSlab(world, 99, 100);
    world.rocks.push_back({{kFeetX - 10, 30, kFeetZ - 10}, {kFeetX + 10, 98.5f, kFeetZ + 10}, false});
    const auto result = plan(kFeet, world);
    CHECK(result.verdict == Verdict::InsideRock);
    CHECK(result.rockReason == RockReason::LandingFace);
    CHECK(result.underside == doctest::Approx(99));
    CHECK(result.crossings == 1); // the line down saw only the branch's underside
    CHECK(result.landing == doctest::Approx(30));
    CHECK(result.landingFace == LandingFace::Back);
    World deep;
    addSlab(deep, 99, 100);
    deep.rocks.push_back({{kFeetX - 10, -150, kFeetZ - 10}, {kFeetX + 10, 98.5f, kFeetZ + 10}, false});
    const auto votes = plan(kFeet, deep);
    CHECK(votes.verdict == Verdict::InsideRock);
    CHECK(votes.rockReason == RockReason::DropVotes);
    CHECK(std::isnan(votes.landing));
    CHECK(votes.rockVotes == 8); // every level line leaves the rock; the line down is too short to
    World unfiltered;
    unfiltered.solidFilter = false;
    addSlab(unfiltered, 99, 100);
    unfiltered.rocks.push_back({{kFeetX - 10, 30, kFeetZ - 10}, {kFeetX + 10, 98.5f, kFeetZ + 10}, false});
    const auto both = plan(kFeet, unfiltered);
    CHECK(both.landingFace == LandingFace::Both);
    CHECK(both.verdict == Verdict::InsideRock);
    CHECK(both.rockReason == RockReason::DropVotes);
    World closed;
    addSlab(closed, 99, 100);
    closed.rocks.push_back({{kFeetX - 10, 30, kFeetZ - 10}, {kFeetX + 10, 98.5f, kFeetZ + 10}});
    CHECK(plan(kFeet, closed).verdict == Verdict::BlockedExit);
}

TEST_CASE("boot 11: rock beside open air and faces reported to both sides do not count as inside rock") {
    World world;
    addSlab(world, 99, 100);
    world.rocks.push_back({{kFeetX + 3, 60, kFeetZ - 20}, {kFeetX + 30, 98.5f, kFeetZ + 20}}); // a cliff to +x
    world.rocks.push_back({{kFeetX - 30, 60, kFeetZ + 5}, {kFeetX + 30, 98.5f, kFeetZ + 30}}); // and to +z
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    CHECK(result.rockVotes == 0);
    World both;
    addSlab(both, 99, 100);
    both.rocks.push_back({{kFeetX - 10, 30, kFeetZ - 10}, {kFeetX + 10, 98.5f, kFeetZ + 10}, false, false});
    const auto inside = plan(kFeet, both);
    CHECK(inside.rockVotes == 0);
    CHECK(inside.valid());
}

TEST_CASE("boot 11: ground just past the drop point no longer blocks the exit") {
    World ground;
    addSlab(ground, 99, 100);
    ground.surfaces.push_back({96.0f, true});
    const auto result = plan(kFeet, ground);
    REQUIRE(result.valid());
    CHECK(result.clearBelow == doctest::Approx(3.0));
    CHECK(result.drop.y > 96.0f); // the drop point stays above the ground
    World shallow;
    addSlab(shallow, 99, 100);
    shallow.surfaces.push_back({96.15f, true});
    CHECK(plan(kFeet, shallow).verdict == Verdict::BlockedExit);
    World airy;
    addSlab(airy, 99, 100);
    CHECK(std::isnan(plan(kFeet, airy).clearBelow));
}

TEST_CASE("boot 12: a floor only the solid-world layer sees is where Link lands, and blocks the exit when close") {
    World close;
    addSlab(close, 99, 100);
    close.surfaces.push_back({97.0f, true, false});
    const auto blocked = plan(kFeet, close);
    CHECK(blocked.verdict == Verdict::BlockedExit);
    CHECK(std::isnan(blocked.clearBelow)); // the ceiling-clipper exit check saw nothing
    CHECK(blocked.landing == doctest::Approx(97));
    World lower;
    addSlab(lower, 99, 100);
    lower.surfaces.push_back({96.5f, true, false});
    const auto landed = plan(kFeet, lower);
    REQUIRE(landed.valid());
    CHECK(landed.landing == doctest::Approx(96.5));
    CHECK(landed.landingFace == LandingFace::Front);
    CHECK(landed.landingVotes == 0);
    CHECK(landed.solidFloor == doctest::Approx(100));
    World silent;
    silent.solidAnswers = false;
    addSlab(silent, 99, 100);
    silent.surfaces.push_back({97.0f, true, false});
    const auto open = plan(kFeet, silent);
    REQUIRE(open.valid());
    CHECK(std::isnan(open.solidFloor));
    CHECK(std::isnan(open.landing));
}

TEST_CASE("boot 12: a landing inside rock is refused") {
    World hollow;
    addSlab(hollow, 99, 100);
    hollow.rocks.push_back({{kFeetX - 10, 50, kFeetZ - 10}, {kFeetX + 10, 90, kFeetZ + 10}, false, true, false});
    const auto inside = plan(kFeet, hollow);
    CHECK(inside.verdict == Verdict::InsideRock);
    CHECK(inside.rockReason == RockReason::LandingFace);
    CHECK(inside.landing == doctest::Approx(50));
    CHECK(inside.rockVotes == -1); // refused before the votes at the drop point
    World floored;
    addSlab(floored, 99, 100);
    floored.rocks.push_back({{kFeetX - 10, 50, kFeetZ - 10}, {kFeetX + 10, 90, kFeetZ + 10}, false, true, false});
    floored.surfaces.push_back({70, true, false});
    const auto votes = plan(kFeet, floored);
    CHECK(votes.verdict == Verdict::InsideRock);
    CHECK(votes.rockReason == RockReason::LandingVotes);
    CHECK(votes.landing == doctest::Approx(70));
    CHECK(votes.landingFace == LandingFace::Front);
    CHECK(votes.landingVotes == 8);
    CHECK(votes.rockVotes == 0);
    World boxed;
    addSlab(boxed, 99, 100);
    boxed.boxes.push_back({97, 99.5f, false});
    const auto start = plan(kFeet, boxed);
    CHECK(start.verdict == Verdict::InsideRock);
    CHECK(start.rockReason == RockReason::LandingStart);
}

TEST_CASE("boot 12: a stray face deep below is cleared by the voxel map or an outside floor above it") {
    VoxelUp slab;
    slab.answer = VoxelUp::Answer::Unit;
    slab.found = true;
    slab.firstSolid = 94.5f; // 1 m cells rounded outward from the faces
    slab.solidEnd = 100.5f;
    const auto withSlab = [&](Vec3) { return slab; };
    World silent{{{100, true}, {95, false}, {55, false}}};
    silent.solidAnswers = false;
    CHECK(plan(kFeet, silent).verdict == Verdict::NoUnderside);
    const auto kept = plan(kFeet, silent, withSlab);
    REQUIRE(kept.valid());
    CHECK(kept.strayKept == StrayKept::Voxel);
    CHECK(kept.stray == doctest::Approx(55));
    CHECK(kept.underside == doctest::Approx(95));
    VoxelUp elsewhere = slab;
    elsewhere.firstSolid = 92; // the map's solid starts 3 m below our underside
    CHECK(plan(kFeet, silent, [&](Vec3) { return elsewhere; }).verdict == Verdict::NoUnderside);
    World floored{{{100, true}, {95, false}, {55, false}, {70, true, false}}};
    const auto landed = plan(kFeet, floored);
    REQUIRE(landed.valid());
    CHECK(landed.strayKept == StrayKept::Landing);
    CHECK(landed.landing == doctest::Approx(70));
    World open{{{100, true}, {95, false}, {55, false}}};
    CHECK(plan(kFeet, open).verdict == Verdict::NoUnderside);
    const auto inside = plan(kFeet, open, withSlab);
    CHECK(inside.verdict == Verdict::InsideRock);
    CHECK(inside.rockReason == RockReason::LandingFace);
    CHECK(inside.strayKept == StrayKept::Voxel);
}

TEST_CASE("a failed upward voxel search does not establish inside rock") {
    World world;
    addSlab(world, 99, 100);
    VoxelUp none;
    none.answer = VoxelUp::Answer::Unit;
    Vec3 asked{};
    const auto result = plan(kFeet, world, [&](Vec3 q) {
        asked = q;
        return none;
    });
    CHECK(result.valid());
    CHECK(asked.y == doctest::Approx(99 - 2.8)); // asked at the drop point
    VoxelUp busy;
    busy.answer = VoxelUp::Answer::Busy;
    CHECK(plan(kFeet, world, [&](Vec3) { return busy; }).valid());
    CHECK(plan(kFeet, world, NoVoxel{}).valid());
}

TEST_CASE("patch03 internal exit stays unverified despite passing collision geometry") {
    // Event 7's measured heights. These planes reproduce the insufficient collision evidence, not a reconstruction of the island mesh. The native query in that event returned unit/false.
    World world{{{1548.669f, true}, {1542.880f, false}, {1539.394f, true}}};
    VoxelUp missing;
    missing.answer = VoxelUp::Answer::Unit;
    auto candidate = plan({kFeetX, 1548.669f, kFeetZ}, world, [&](Vec3) { return missing; });
    REQUIRE(candidate.valid());
    CHECK(candidate.underside == doctest::Approx(1542.880f));
    confirmExit(candidate);
    CHECK(candidate.verdict == Verdict::UnverifiedExit);
    CHECK(candidate.rockReason == RockReason::None); // search failure is not an occupancy claim
}

TEST_CASE("native exit confirmation requires both boundaries and retries unavailable data") {
    Plan geometry;
    geometry.verdict = Verdict::Valid;
    geometry.floor = 1538.891f; // patch03 event 18, successful native map
    geometry.underside = 1529.843f;
    geometry.voxel = {VoxelUp::Answer::Unit, true, 1529.5f, 1539.5f, 1544.5f};
    auto verified = geometry;
    confirmExit(verified);
    CHECK(verified.valid());
    for (const auto answer : {VoxelUp::Answer::NoModule, VoxelUp::Answer::Busy, VoxelUp::Answer::NoUnit}) {
        auto unknown = geometry;
        unknown.voxel.answer = answer;
        confirmExit(unknown);
        CHECK(unknown.verdict == Verdict::UnverifiedExit);
    }
    for (int boundary = 0; boundary < 2; ++boundary) {
        auto differentSlab = geometry;
        (boundary ? differentSlab.voxel.solidEnd : differentSlab.voxel.firstSolid) -= 4;
        confirmExit(differentSlab);
        CHECK(differentSlab.verdict == Verdict::UnverifiedExit);
    }
    auto noHeights = geometry;
    noHeights.voxel.firstSolid = NAN;
    confirmExit(noHeights);
    CHECK(noHeights.verdict == Verdict::UnverifiedExit);
    auto blocked = geometry;
    blocked.verdict = Verdict::TightExit;
    confirmExit(blocked);
    CHECK(blocked.verdict == Verdict::TightExit);
}

TEST_CASE("a floor kept only by the other side value is a shell stood on from inside") {
    World swapped;
    swapped.frontValue = Faces::OtherSide;
    addSlab(swapped, 99, 100);
    const auto result = plan(kFeet, swapped);
    CHECK(result.verdict == Verdict::StandingInside);
    CHECK(result.entry == Faces::OtherSide);
}

TEST_CASE("boot 3: standing inside a hollow island on its down-facing shell is refused") {
    World world{{{100, false}, {60, true}}};
    const auto result = plan(kFeet, world);
    CHECK(result.verdict == Verdict::StandingInside);
    CHECK(std::isnan(result.oneSideY));
    CHECK(result.otherSideY == doctest::Approx(100));
}

TEST_CASE("boot 3: a hollow island's stray inner face is not its underside") {
    World event1{{{100, true}, {92.3f, false}, {87.6f, false}, {40, true}}};
    const auto first = plan(kFeet, event1);
    REQUIRE(first.valid());
    CHECK(first.underside == doctest::Approx(87.6));
    CHECK(first.thickness == doctest::Approx(12.4));
    CHECK(first.crossings == 3); // with the ground, within reach since boot 11
    World event3{{{100, true}, {98.3f, false}, {90.5f, false}}};
    const auto third = plan(kFeet, event3);
    REQUIRE(third.valid());
    CHECK(third.underside == doctest::Approx(90.5));
    CHECK(third.drop.y == doctest::Approx(90.5 - 2.8));
}

TEST_CASE("a closed piece inside a hollow island is passed to the shell below it") {
    World world{{{100, true}, {96, false}, {90, true}, {89, false}, {85, false}}};
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    CHECK(result.underside == doctest::Approx(85));
    CHECK(result.crossings == 4);
}

TEST_CASE("a stray down-facing face past the thickness limit refuses the target") {
    World world{{{100, true}, {95, false}, {55, false}}}; // 70 m until the limit rose to 40 m after boot 11
    CHECK(plan(kFeet, world).verdict == Verdict::NoUnderside);
}

TEST_CASE("a bridge over stacked ground layers is accepted") {
    World world;
    addSlab(world, 99, 100);
    world.surfaces.push_back({95, true});
    world.surfaces.push_back({94.87f, true});
    world.surfaces.push_back({94.03f, true});
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    CHECK(result.underside == doctest::Approx(99));
}

TEST_CASE("boot 1: plain ground is never a slab, even though rays hit it from below") {
    World world{{{100, true}}};
    const auto result = plan(kFeet, world);
    CHECK(result.verdict == Verdict::NoUnderside);
    CHECK(result.floor == doctest::Approx(100));
}

TEST_CASE("boot 2: stacked ground surfaces inside a landmass are not an underside") {
    World world{{{100, true}, {99.87f, true}, {99.03f, true}}};
    const auto result = plan(kFeet, world);
    CHECK(result.verdict == Verdict::NoUnderside);
}

TEST_CASE("closed pieces buried in a slab are passed through to the slab's real underside") {
    World world;
    addSlab(world, 96, 100);
    addSlab(world, 99.2f, 99.5f);
    addSlab(world, 97.5f, 98.8f);
    world.surfaces.push_back({40, true});
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    CHECK(result.underside == doctest::Approx(96));
    CHECK(result.crossings == 6); // with the ground, within reach since boot 11
}

TEST_CASE("a buried piece with no bottom face leaves the walk inside the solid") {
    World world;
    addSlab(world, 98, 100);
    world.surfaces.push_back({99.4f, true});
    CHECK(plan(kFeet, world).verdict == Verdict::NoUnderside);
}

TEST_CASE("ground over a cave ceiling within reach descends into the cave") {
    World world{{{100, true}, {94, false}, {80, true}}};
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    CHECK(result.underside == doctest::Approx(94));
    CHECK(result.drop.y == doctest::Approx(94 - 2.8));
}

TEST_CASE("feet slightly sunk into the floor still find it") {
    World world;
    addSlab(world, 99, 100);
    const auto result = plan({10, 99.97f, -5}, world);
    REQUIRE(result.valid());
    CHECK(result.floor == doctest::Approx(100));
    CHECK(result.target.y == doctest::Approx(99.5));
}

TEST_CASE("a very thin slab keeps the travel target just under the floor") {
    World world;
    addSlab(world, 99.7f, 100);
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    CHECK(result.target.y == doctest::Approx(99.9));
}

TEST_CASE("ground close below the underside blocks the exit") {
    World world;
    addSlab(world, 99, 100);
    world.surfaces.push_back({96.5f, true});
    const auto result = plan(kFeet, world);
    CHECK(result.verdict == Verdict::BlockedExit);
    CHECK_FALSE(result.valid());
}

TEST_CASE("ground just beyond the exit clearance is accepted") {
    World world;
    addSlab(world, 99, 100);
    world.surfaces.push_back({95.0f, true});
    CHECK(plan(kFeet, world).valid());
}

TEST_CASE("a plank resting on, set into, or level with something solid is rejected") {
    World resting{{{100, true}, {99.7f, false}, {99.68f, true}}};
    CHECK(plan(kFeet, resting).verdict == Verdict::BlockedExit);
    World embedded{{{100, true}, {99.75f, true}, {99.7f, false}}};
    CHECK_FALSE(plan(kFeet, embedded).valid());
    World level;
    addSlab(level, 99, 100);
    addSlab(level, 95, 99);
    CHECK(plan(kFeet, level).verdict == Verdict::BlockedExit);
}

TEST_CASE("nothing under the feet means no floor") {
    World world{{{90, true}}};
    CHECK(plan(kFeet, world).verdict == Verdict::NoFloor);
}

TEST_CASE("thickness limits") {
    World thin;
    addSlab(thin, 99.95f, 100);
    CHECK(plan(kFeet, thin).verdict == Verdict::TooThin);
    World thick;
    addSlab(thick, 55, 100);
    CHECK(plan(kFeet, thick).verdict == Verdict::NoUnderside);
    World deep;
    addSlab(deep, 65, 100);
    CHECK(plan(kFeet, deep).valid());
}

TEST_CASE("without a working one-sided filter nothing is accepted") {
    World world;
    world.filterWorks = false;
    addSlab(world, 99, 100);
    const auto result = plan(kFeet, world);
    CHECK(result.verdict == Verdict::NoFaceFilter);
    CHECK(result.oneSideY == doctest::Approx(100));
    CHECK(result.otherSideY == doctest::Approx(100));
}

TEST_CASE("a box-shaped deck is refused rather than guessed") {
    World unfiltered;
    unfiltered.boxes.push_back({99, 100});
    CHECK(plan(kFeet, unfiltered).verdict == Verdict::NoFaceFilter);
    World filtered;
    filtered.boxesIgnoreFilter = false;
    filtered.boxes.push_back({99, 100});
    CHECK(plan(kFeet, filtered).verdict == Verdict::InsideSolid);
    World silent;
    silent.boxesIgnoreFilter = false;
    silent.insideHits = false;
    silent.boxes.push_back({99, 100});
    CHECK(plan(kFeet, silent).verdict == Verdict::NoUnderside);
}

TEST_CASE("the planner's cast budget") {
    World world;
    addSlab(world, 99, 100);
    plan(kFeet, world);
    CHECK(world.casts == 9 + tuning::kRockCasts + 2 * tuning::kBodyCasts +
                         2 * tuning::kViewDirections * tuning::kViewLines);
    World ground;
    addSlab(ground, 99, 100);
    ground.surfaces.push_back({40, true});
    plan(kFeet, ground);
    CHECK(ground.casts == world.casts + 1 + tuning::kLandingCasts - 1);
    World layered;
    for (int i = 0; i < 20; ++i) addSlab(layered, 99.5f - i * 0.4f, 99.7f - i * 0.4f);
    addSlab(layered, 90.5f, 100);
    const auto result = plan(kFeet, layered);
    CHECK(result.verdict == Verdict::NoUnderside);
    CHECK(result.crossings == tuning::kMaxCrossings);
    CHECK(layered.casts <= 6 + tuning::kMaxCrossings);
}

TEST_CASE("underside camera sits below the slab behind Link and looks at his legs") {
    const Pose native{{0, 105, -10}, {0, 101, 0}};
    const auto direction = horizontalDirection(native);
    CHECK(direction.z == doctest::Approx(1));
    const Vec3 behind{-direction.x, 0, -direction.z};
    const int view = chooseView(0xFF, behind);
    CHECK(view == 4);
    const auto pose = previewPose({3, 99.5f, 4}, 99, view, behind);
    CHECK(pose.lookAt.x == doctest::Approx(3));
    CHECK(pose.lookAt.y == doctest::Approx(97.8));
    CHECK(pose.lookAt.z == doctest::Approx(4));
    CHECK(pose.position.x == doctest::Approx(3).epsilon(1e-4));
    CHECK(pose.position.y == doctest::Approx(98));
    CHECK(pose.position.z == doctest::Approx(4 - 4.0));
}

TEST_CASE("with no open side the camera looks up at the legs from inside the open column") {
    const auto pose = previewPose({3, 99.5f, 4}, 99, -1, {0, 0, -1});
    CHECK(pose.position.y == doctest::Approx(99 - 1.6));
    CHECK(tuning::kBelowDepth < tuning::kExitClearance);
    CHECK(pose.position.z == doctest::Approx(4 - 0.2));
    CHECK(pose.lookAt.y == doctest::Approx(99 - 1.2));
}

TEST_CASE("solid-only side walls block the body while a clear sloping landing remains usable") {
    World world;
    addSlab(world, 99, 100);
    world.surfaces.push_back({95, true, false, 0.5f});
    CHECK(plan(kFeet, world).valid());
    const Vec3 waist{kFeetX, 97.7f, kFeetZ};
    const Vec3 edge{kFeetX + 0.4f, 97.7f, kFeetZ};
    world.rocks.push_back({{kFeetX + 0.2f, 96.8f, kFeetZ - 2},
                           {kFeetX + 2, 99, kFeetZ + 2}, true, true, false});
    REQUIRE_FALSE(world(waist, edge, Faces::All, Layer::Clipper).hit);
    REQUIRE(world(waist, edge, Faces::All, Layer::Solid).hit);
    CHECK(plan(kFeet, world).verdict == Verdict::TightExit);
    world.rocks.back().clipper = true;
    CHECK(plan(kFeet, world).verdict == Verdict::TightExit);
}

TEST_CASE("fallback preview and its orbit remain inside a narrow clear column") {
    World world;
    addSlab(world, 99, 100);
    boxIn(world, 0.45f);
    const auto result = plan(kFeet, world);
    REQUIRE(result.valid());
    REQUIRE(result.openViews == 0);
    PreviewOrbit orbit;
    orbit.begin(previewPose(result.target, 99, -1, {0, 0, -1}));
    for (int i = 0; i < 160; ++i) {
        orbit.move(1, i < 80 ? 1.0f : -1.0f, 0.05f);
        const auto pose = clearPreviewPose(orbit.pose(result.target, 99), world);
        REQUIRE(pose.has_value());
        CHECK(distance(pose->position, pose->lookAt) > 0.05f);
        CHECK_FALSE(world(pose->position, pose->lookAt, Faces::All, Layer::Solid).hit);
        CHECK(std::fabs(pose->position.x - kFeetX) < 0.45f);
        CHECK(std::fabs(pose->position.z - kFeetZ) < 0.45f);
    }
}

TEST_CASE("preview orbit responds to the stick and clamps to solid-only obstructions") {
    World world;
    addSlab(world, 99, 100);
    const auto initial = previewPose(kFeet, 99, 0, {0, 0, 1});
    PreviewOrbit orbit;
    orbit.begin(initial);
    orbit.move(0.1f, -0.1f, 0.05f);
    CHECK(distance(orbit.pose(kFeet, 99).position, initial.position) < 0.001f);
    for (int i = 0; i < 20; ++i) orbit.move(1, 0, 0.05f);
    const auto wanted = orbit.pose(kFeet, 99);
    CHECK(wanted.position.x == doctest::Approx(kFeetX - 4));
    world.rocks.push_back({{kFeetX - 5, 96, kFeetZ - 5},
                           {kFeetX - 1, 99, kFeetZ + 5}, true, true, false});
    REQUIRE_FALSE(world(wanted.lookAt, wanted.position, Faces::All, Layer::Clipper).hit);
    REQUIRE(world(wanted.lookAt, wanted.position, Faces::All, Layer::Solid).hit);
    const auto safe = clearPreviewPose(wanted, world);
    REQUIRE(safe.has_value());
    CHECK(safe->position.x > kFeetX - 1);
    CHECK_FALSE(world(safe->lookAt, safe->position, Faces::All, Layer::Solid).hit);
    const auto blocked = [](Vec3 from, Vec3, Faces, Layer) { return CastResult{true, from, {}}; };
    CHECK_FALSE(clearPreviewPose(wanted, blocked).has_value());
    CHECK_FALSE(clearPreviewPose(Pose{wanted.lookAt, wanted.lookAt}, world).has_value());
}

TEST_CASE("boot 5: the sink camera moves down with Link on the preview's side") {
    const Vec3 behind{0, 0, -1};
    const int view = chooseView(0xFF, behind);
    const auto top = trackPose({3, 110, 4}, view, behind);
    const auto bottom = trackPose({3, 99.5f, 4}, view, behind);
    CHECK(top.position.y == doctest::Approx(111.2));
    CHECK(top.lookAt.y == doctest::Approx(110.9));
    CHECK(bottom.position.y == doctest::Approx(100.7));
    CHECK(bottom.lookAt.y == doctest::Approx(100.4));
    CHECK(bottom.lookAt.x == doctest::Approx(3));
    CHECK(bottom.lookAt.z == doctest::Approx(4));
    const auto preview = previewPose({3, 99.5f, 4}, 99, view, behind);
    CHECK(bottom.position.x == doctest::Approx(preview.position.x).epsilon(1e-4));
    CHECK(bottom.position.z == doctest::Approx(preview.position.z));
    CHECK(trackPose({3, 99.5f, 4}, -1, behind).position.z == doctest::Approx(0));
}

TEST_CASE("boot 5: after A the camera stays at the preview spot and follows Link down out of the slab") {
    const Vec3 behind{0, 0, -1};
    const int view = chooseView(0xFF, behind);
    const Vec3 target{3, 99.5f, 4};
    const auto preview = previewPose(target, 99, view, behind);
    const auto start = dropPose(target, target, 99, view, behind);
    CHECK(start.position.y == preview.position.y);
    CHECK(start.lookAt.y == preview.lookAt.y);
    const auto out = dropPose({3, 96.2f, 4}, target, 99, view, behind);
    CHECK(out.position.y == preview.position.y);
    CHECK(out.lookAt.y == doctest::Approx(97.1));
}

TEST_CASE("a camera looking straight down still gets a horizontal direction") {
    const auto direction = horizontalDirection({{1, 10, 1}, {1, 0, 1}});
    CHECK(direction.z == 1);
    CHECK(direction.x == 0);
}

TEST_CASE("boot 6: only cameras near Link are driven") {
    const Vec3 link{600, 1520, 1400};
    CHECK(cameraInRange({604, 1534, 1405}, link));
    CHECK(cameraInRange({600, 1580, 1400}, link));
    CHECK_FALSE(cameraInRange({600, 1580.1f, 1400}, link));
    CHECK_FALSE(cameraInRange({0, 0, 0}, link));
    CHECK_FALSE(cameraInRange({std::nanf(""), 1520, 1400}, link));
}

TEST_CASE("boot 7: the camera's up direction has no roll") {
    const Pose views[]{{{622.4f, 1518.5f, 1422.8f}, {618.4f, 1518.3f, 1422.8f}}, {{3, 95.6f, 4.5f}, {3, 98.4f, 4}}};
    for (const auto& pose : views) {
        Vec3 up{};
        REQUIRE(levelUp(pose, up));
        Vec3 f{pose.lookAt.x - pose.position.x, pose.lookAt.y - pose.position.y, pose.lookAt.z - pose.position.z};
        const float length = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
        f = {f.x / length, f.y / length, f.z / length};
        CHECK(up.x * f.x + up.y * f.y + up.z * f.z == doctest::Approx(0).epsilon(1e-5));
        CHECK(up.x * up.x + up.y * up.y + up.z * up.z == doctest::Approx(1));
        CHECK(up.y > 0);
        CHECK(-up.x * f.z + up.z * f.x == doctest::Approx(0).epsilon(1e-5));
    }
    Vec3 up{};
    CHECK_FALSE(levelUp({{0, 10, 0}, {0, 0, 0}}, up));
    CHECK_FALSE(levelUp({{1, 1, 1}, {1, 1, 1}}, up));
    CHECK_FALSE(levelUp({{std::nanf(""), 0, 0}, {0, 0, 1}}, up));
}

TEST_CASE("boot 7: the fall after A speeds up and never passes the game's own speed") {
    CHECK(dropStep(0) == doctest::Approx(1.0 / 30));
    CHECK(dropStep(-1) == doctest::Approx(1.0 / 30));
    CHECK(dropStep(0.5f) == doctest::Approx(5.9 / 30));
    CHECK(dropStep(10) == doctest::Approx(0.67));
    float covered = 0;
    int steps = 0;
    while (covered < 2.3f) covered += dropStep(static_cast<float>(steps++) / 30);
    CHECK(steps > 12);
    CHECK(steps < 24);
}

TEST_CASE("camera blend eases from one pose to the next and holds at either end") {
    const Pose a{{0, 0, 0}, {0, 0, 1}}, b{{10, 10, 10}, {5, 5, 5}};
    CHECK(blend(a, b, 0).position.x == 0);
    CHECK(blend(a, b, -1).position.x == 0);
    CHECK(blend(a, b, 1).position.x == doctest::Approx(10));
    CHECK(blend(a, b, 3).position.x == doctest::Approx(10));
    CHECK(blend(a, b, 0.5f).position.x == doctest::Approx(5));
    CHECK(blend(a, b, 0.25f).position.x < 2.5f);
}

static float offCentre(const Pose& pose, Vec3 point) {
    const float vx = pose.lookAt.x - pose.position.x, vy = pose.lookAt.y - pose.position.y,
                vz = pose.lookAt.z - pose.position.z;
    const float px = point.x - pose.position.x, py = point.y - pose.position.y, pz = point.z - pose.position.z;
    const float dot = vx * px + vy * py + vz * pz;
    const float lengths = std::sqrt((vx * vx + vy * vy + vz * vz) * (px * px + py * py + pz * pz));
    return std::acos(std::fmin(1.0f, dot / lengths)) * 57.29578f;
}

TEST_CASE("boot 10: the Descend aim view starts above and behind Link with all of him in view") {
    const Vec3 link{3, 100, 4};
    const Vec3 view{0, 0, 1}; // the player looks toward +Z
    const auto pose = aimPose(link, view, tuning::kAimPitch, 1);
    CHECK(pose.position.x == doctest::Approx(3));
    CHECK(pose.position.z < 4 - 2.0f);   // behind him
    CHECK(pose.position.y > 100 + 3.5f); // well above him
    CHECK(pose.lookAt.y == doctest::Approx(100 + tuning::kAimPivot));
    const float down = -pitchUp(pose) * 57.29578f;
    CHECK(down > 50);
    CHECK(down < 60);
    CHECK(offCentre(pose, link) < 15);
    CHECK(offCentre(pose, {3, 101.7f, 4}) < 15);
    Vec3 up{};
    CHECK(levelUp(pose, up));
    const auto half = aimPose(link, view, tuning::kAimPitch, 0.5f);
    CHECK(half.position.y == doctest::Approx((aimPivot(link).y + pose.position.y) / 2));
    CHECK(half.position.z == doctest::Approx((4 + pose.position.z) / 2));
}

TEST_CASE("boot 10: the right stick tilts the aim view from where it started") {
    CHECK(aimPitch(1.2f, 1.2f) == doctest::Approx(tuning::kAimPitch));
    CHECK(aimPitch(1.3f, 1.2f) == doctest::Approx(tuning::kAimPitch - 0.1f));
    CHECK(aimPitch(1.1f, 1.2f) == doctest::Approx(tuning::kAimPitch + 0.1f));
    CHECK(aimPitch(-2.0f, 1.2f) == doctest::Approx(tuning::kAimPitchMax));
    CHECK(aimPitch(3.0f, 1.2f) == doctest::Approx(tuning::kAimPitchMin));
    const Vec3 link{3, 100, 4};
    const auto low = aimPose(link, {0, 0, 1}, tuning::kAimPitchMin, 1);
    const auto high = aimPose(link, {0, 0, 1}, tuning::kAimPitchMax, 1);
    CHECK(high.position.y > low.position.y);
    Vec3 up{};
    CHECK(levelUp(low, up));
    CHECK(levelUp(high, up));
    CHECK(aimPose(link, {1, 0, 0}, tuning::kAimPitch, 1).position.x < 3 - 2.0f);
    CHECK(pitchUp({{0, 0, 0}, {0, 1, 1}}) == doctest::Approx(0.7853982f));
}

TEST_CASE("boot 9: the aim camera stops short of a wall behind Link") {
    const Vec3 link{3, 100, 4};
    const Vec3 pivot = aimPivot(link), spot = aimSpot(link, {0, 0, 1}, tuning::kAimPitch);
    CHECK(aimReach(pivot, spot, {}) == 1);
    const CastResult halfway{true, lerp(pivot, spot, 0.5f), {}};
    CHECK(aimReach(pivot, spot, halfway) == doctest::Approx(0.5f - tuning::kAimMargin / tuning::kAimDistance));
    const CastResult close{true, lerp(pivot, spot, 0.05f), {}};
    CHECK(aimReach(pivot, spot, close) == doctest::Approx(tuning::kAimMinReach));
}

TEST_CASE("boot 10: the way back after B ends once collision is back and Link's feet are above his start") {
    CHECK_FALSE(returnReleases(false, 101.0f, 100.0f)); // still passing through the slab
    CHECK_FALSE(returnReleases(true, 99.9f, 100.0f));   // collision back, but his feet are still below the floor
    CHECK(returnReleases(true, 100.0f, 100.0f));
    CHECK(returnReleases(true, 100.05f, 100.0f));
}

TEST_CASE("boot 9: the aim view blends in and out from wherever it was") {
    AimBlend blend;
    CHECK(blend.weight(1000) == 0);
    CHECK(blend.turn(true, 1000));
    CHECK_FALSE(blend.turn(true, 1050));
    CHECK(blend.weight(1000) == 0);
    CHECK(blend.weight(1000 + tuning::kAimBlendMs / 2) == doctest::Approx(0.5));
    CHECK(blend.weight(1000 + tuning::kAimBlendMs * 2) == 1);
    CHECK(blend.turn(false, 1000 + tuning::kAimBlendMs / 2));
    CHECK(blend.weight(1000 + tuning::kAimBlendMs * 3 / 4) == doctest::Approx(0.25));
    CHECK(blend.weight(1000 + tuning::kAimBlendMs) == 0);
    CHECK(blend.weight(0) <= 1); // a clock read before the turn stays within 0..1
    CHECK(blend.weight(0) >= 0);
}

TEST_CASE("camera phases follow the event's timeline") {
    Timeline t;
    CHECK(phaseAt(t, 1000) == Phase::Off);
    t.start = t.touched = 1000;
    CHECK(phaseAt(t, 1000) == Phase::Sink);
    t.touched = 2800;
    t.preview = 2800;
    CHECK(phaseAt(t, 2810) == Phase::Preview);
    CHECK(phaseAt(t, 2800 + 501) == Phase::Preview);
    CHECK(phaseAt(t, 2800 + 500 + 601) == Phase::Preview);
    t.touched = 5000;
    CHECK(phaseAt(t, 5010) == Phase::Preview);
    t.leave = 6000;
    t.drop = true;
    CHECK(phaseAt(t, 6010) == Phase::Drop);
    t.cancel = t.touched = 6500;
    CHECK(phaseAt(t, 6550) == Phase::Drop);
    CHECK(phaseAt(t, 10000) == Phase::Drop);
    t.released = true;
    CHECK(phaseAt(t, 6601) == Phase::Handback);
    CHECK(phaseAt(t, 6600 + 600) == Phase::Handback);
    CHECK(phaseAt(t, 6600 + 601) == Phase::Off);
    t.drop = false;
    t.released = false;
    t.cancel = 0;
    CHECK(phaseAt(t, 6010) == Phase::Rise);
    CHECK(phaseAt(t, 6000 + 3001) == Phase::Rise);
    CHECK(phaseAt(t, 6000 + 3000 + 601) == Phase::Rise);
    t.cancel = t.touched = 12400;
    CHECK(phaseAt(t, 12423) == Phase::Rise);
    CHECK(phaseAt(t, 20000) == Phase::Rise);
    t.released = true;
    CHECK(phaseAt(t, 12501) == Phase::Handback);
    t.released = false;
    t.drop = true;
    CHECK(phaseAt(t, 12423) == Phase::Drop);
}

TEST_CASE("each camera phase settles on its own pose") {
    const Vec3 behind{0, 0, -1};
    const CameraScene scene{{3, 97, 4}, {3, 99.5f, 4}, 99, chooseView(0xFF, behind), behind, {{1, 2, 3}, {4, 5, 6}}};
    CHECK(phasePose(Phase::Sink, scene).lookAt.y == doctest::Approx(97.9)); // tracking Link, as on the way back
    CHECK(phasePose(Phase::Rise, scene).lookAt.y == doctest::Approx(97.9));
    CHECK(phasePose(Phase::Preview, scene).lookAt.y == doctest::Approx(97.8));
    CHECK(phasePose(Phase::Drop, scene).lookAt.y == doctest::Approx(97.8));
    CHECK(phasePose(Phase::Handback, scene).position.x == 1);
    CHECK(blendMs(Phase::Sink) == 300);
    CHECK(blendMs(Phase::Preview) == 180);
    CHECK(blendMs(Phase::Handback) == 600);
}

TEST_CASE("boot 12: on the way down the camera tracks Link from beside him") {
    const Vec3 behind{0, 0, -1};
    const int view = chooseView(0xFF, behind);
    CameraScene scene{{3, 99.9f, 4}, {3, 90.5f, 4}, 90, view, behind, {}};
    for (const float y : {99.9f, 95.0f, 90.5f}) {
        scene.link.y = y;
        const auto sink = phasePose(Phase::Sink, scene);
        CHECK(sink.position.y == doctest::Approx(y + tuning::kTrackHeight));
        CHECK(sink.lookAt.y == doctest::Approx(y + tuning::kTrackLook));
        CHECK(offCentre(sink, {3, y, 4}) < 15);
        CHECK(offCentre(sink, {3, y + 1.7f, 4}) < 15);
    }
    const auto preview = previewPose(scene.target, scene.underside, view, behind);
    CHECK(phasePose(Phase::Sink, scene).position.z == doctest::Approx(preview.position.z));
    CHECK(phasePose(Phase::Sink, scene).position.x == doctest::Approx(preview.position.x).epsilon(1e-4));
}

TEST_CASE("the stick click toggles Descend only while aiming and clears after aiming ends") {
    ModeSwitch mode;
    CHECK_FALSE(mode.update(false, false, true));
    CHECK_FALSE(mode.descend);
    mode.update(true, false, false);
    CHECK(mode.update(true, false, true));
    CHECK(mode.descend);
    CHECK_FALSE(mode.update(true, false, true)); // held, not a new press
    CHECK(mode.descend);
    mode.update(true, false, false);
    CHECK(mode.update(true, false, true));
    CHECK_FALSE(mode.descend);
    mode.update(true, false, false);
    mode.update(true, false, true);
    CHECK(mode.descend);
    CHECK(mode.update(false, true, false));
    CHECK_FALSE(mode.descend);
}

TEST_CASE("bitwise target identity") {
    CHECK(sameBits({1, 2, 3}, {1, 2, 3}));
    CHECK_FALSE(sameBits({1, 2, 3}, {1, 2, 3.0001f}));
}

TEST_CASE("Deep terrain is nominated by positive ceiling and floor data") {
    Plan shallow;
    shallow.floor = 100;
    shallow.entry = kEntryFaces;
    shallow.verdict = Verdict::NoUnderside;
    const Vec3 feet{0,100,0};
    auto down = [](Vec3, bool ceiling) { return VoxelSurface{true, ceiling ? -400.0f : -900.0f}; };
    auto up = [](Vec3) { return VoxelUp{VoxelUp::Answer::Unit,true,-400,100,NAN}; };
    const auto p = deepCandidate(feet,shallow,down,up);
    CHECK(p.valid());
    CHECK(p.route == Plan::Route::DeepTerrain);
    CHECK(p.thickness == 500);
    CHECK(p.target.y == doctest::Approx(-399.5f));
    CHECK(p.drop.y == doctest::Approx(-402.8f));
    SUBCASE("Absent lower map is not empty space") {
        auto missing = [](Vec3, bool ceiling) { return VoxelSurface{ceiling,ceiling?-400.0f:NAN}; };
        CHECK_FALSE(deepCandidate(feet,shallow,missing,up).valid());
    }
    SUBCASE("A cave floor does not establish a different slab top") {
        auto wrong = [](Vec3) { return VoxelUp{VoxelUp::Answer::Unit,true,-400,110,NAN}; };
        CHECK_FALSE(deepCandidate(feet,shallow,down,wrong).valid());
    }
    SUBCASE("The body's side needs positive map clearance too") {
        auto side = [](Vec3 q) { return VoxelUp{VoxelUp::Answer::Unit,true,q.x>0?-410.0f:-400.0f,100,NAN}; };
        CHECK_FALSE(deepCandidate(feet,shallow,down,side).valid());
    }
    SUBCASE("Short or obstructed paths cannot evade the old planner") {
        shallow.verdict=Verdict::TightExit;
        CHECK_FALSE(deepCandidate(feet,shallow,down,up).valid());
        shallow.verdict=Verdict::NoUnderside;
        auto close = [](Vec3, bool ceiling) { return VoxelSurface{true,ceiling?80.0f:-900.0f}; };
        CHECK_FALSE(deepCandidate(feet,shallow,close,up).valid());
    }
    SUBCASE("Missing physics after preload refuses the exit") {
        auto missing = [](Vec3,Vec3,Faces,Layer){return CastResult{};};
        CHECK_FALSE(loadedDeepExit(p,missing,up).valid());
    }
    SUBCASE("Loaded physical underside confirms the deep candidate") {
        World loaded;
        loaded.surfaces = {{100,true},{-400,false},{-900,true}};
        const auto checked = loadedDeepExit(p,loaded,up);
        REQUIRE(checked.valid());
        CHECK(checked.underside == -400);
        CHECK(checked.target.y == doctest::Approx(-399.5f));
        loaded.surfaces.push_back({-401,true});
        CHECK_FALSE(loadedDeepExit(p,loaded,up).valid());
    }
}

TEST_CASE("Final terrain survey spreads identical safety queries across frames") {
    World world;
    world.surfaces={{100,true},{-400,false},{-450,true}};
    Plan candidate; candidate.route=Plan::Route::DeepTerrain; candidate.floor=100;
    setDestination(candidate,{0,100,0},-400);
    auto up=[](Vec3){return VoxelUp{VoxelUp::Answer::Unit,true,-400,100,NAN};};
    SUBCASE("Late side obstruction is still rejected") {
        world.walls.push_back({true,0.3f,-402,-400.5f});
    }
    const auto full=loadedDeepExit(candidate,world,up,false);
    const int fullCalls=world.casts;
    world.casts=0;
    ExitSurvey survey;
    Plan result;
    int frames=0;
    do {
        const int before=world.casts;
        result=survey.advance(candidate,world,up);
        CHECK(world.casts-before <= 16);
        REQUIRE(++frames < 12);
    } while (result.verdict == Verdict::Pending);
    CHECK(frames > 1);
    CHECK(world.casts == fullCalls);
    CHECK(result.verdict == full.verdict);
    CHECK(result.bodyLine == full.bodyLine);
    CHECK(result.underside == full.underside);
    CHECK(result.landing == full.landing);
    CHECK(survey.count <= ExitSurvey::kCapacity);
    survey.count=survey.voxelCount=0;
    world.surfaces.push_back({-401,true});
    CHECK_FALSE(survey.advance(candidate,world,up).valid());
}

TEST_CASE("Physics objects require two outside hits on the same body") {
    World world;
    const BodyKey key{1,2,3};
    world.boxes.push_back({5,10,true,key});
    const Vec3 feet{kFeetX,10,kFeetZ};
    const auto bodyCast = [&](Vec3 a, Vec3 b) {
        BodyCastResult hit;
        static_cast<CastResult&>(hit) = world(a,b,Faces::All,Layer::Solid,&hit.body);
        return hit;
    };
    const auto floor = bodyCast(at(feet,10.3f),at(feet,8.5f));
    REQUIRE(floor.body == key);
    auto p=objectPlan(feet,floor,world,NoVoxel{},bodyCast,false);
    CHECK(p.valid());
    CHECK(p.underside == 5);
    CHECK(p.route == Plan::Route::Object);
    SUBCASE("Unknown or recycled body is refused") {
        auto unowned=floor; unowned.body={};
        CHECK_FALSE(objectPlan(feet,unowned,world,NoVoxel{},bodyCast,false).valid());
        unowned=floor; ++unowned.body.id;
        CHECK_FALSE(objectPlan(feet,unowned,world,NoVoxel{},bodyCast,false).valid());
    }
    SUBCASE("Objects resting on the ground are refused") {
        world.surfaces.push_back({5,true});
        CHECK_FALSE(objectPlan(feet,floor,world,NoVoxel{},bodyCast,false).valid());
    }
    SUBCASE("A nearby solid-world obstacle still blocks an object exit") {
        world.boxes.push_back({2,4,false,{1,5,6}});
        CHECK_FALSE(objectPlan(feet,floor,world,NoVoxel{},bodyCast,false).valid());
    }
    SUBCASE("Only identified bodies can omit terrain confirmation") {
        p.body={}; confirmExit(p); CHECK_FALSE(p.valid());
        p.verdict=Verdict::Valid; p.route=Plan::Route::Terrain; p.body=key;
        confirmExit(p); CHECK_FALSE(p.valid());
    }
}

TEST_CASE("A platform footprint uses adjoining bodies' lowest underside") {
    World world;
    const Vec3 feet{kFeetX,10,kFeetZ};
    const BodyKey anchor{1,2,3};
    world.boxes = {{9.7f,10,true,anchor,9.9f,10.1f,-6,-4},
                   {8.8f,10,true,{1,4,5},9.5f,9.9f,-6,-4},
                   {8.8f,10,true,{1,6,7},10.1f,10.5f,-6,-4}};
    const auto bodyCast = [&](Vec3 a,Vec3 b) {
        BodyCastResult hit;
        static_cast<CastResult&>(hit)=world(a,b,Faces::All,Layer::Solid,&hit.body);
        return hit;
    };
    const auto floor=bodyCast(at(feet,10.3f),at(feet,8.5f));
    REQUIRE(floor.body == anchor);
    Plan old; old.floor=10; old.body=anchor; old.route=Plan::Route::Object;
    setDestination(old,feet,9.7f);
    CHECK(checkExit(old,feet,NAN,world,NoVoxel{},false).verdict == Verdict::TightExit);
    const auto check=[&] { return objectPlan(feet,floor,world,NoVoxel{},bodyCast,false); };
    const auto combined=check();
    REQUIRE(combined.valid());
    CHECK(combined.body == anchor);
    CHECK(combined.underside == doctest::Approx(8.8f));
    SUBCASE("An isolated narrow piece retains its previous acceptance") {
        world.boxes.resize(1);
        CHECK(check().valid());
        CHECK(check().underside == doctest::Approx(9.7f));
    }
    SUBCASE("Fresh geometry follows a moved neighbour") {
        world.boxes[2].bottom=8.4f;
        CHECK(check().valid());
        CHECK(check().underside == doctest::Approx(8.4f));
    }
    SUBCASE("The lower envelope still needs clear space to drop") {
        world.surfaces.push_back({8,true});
        CHECK_FALSE(check().valid());
    }
    SUBCASE("An unrelated obstruction is not mistaken for part of the deck") {
        world.boxes.push_back({7.5f,8,false,{1,8,9},9.7f,10.3f,-6,-4});
        CHECK_FALSE(check().valid());
    }
    SUBCASE("A missing or different-world neighbour identity fails closed") {
        world.boxes[2].body={}; CHECK_FALSE(check().valid());
        world.boxes[2].body={2,6,7}; CHECK_FALSE(check().valid());
    }
}

TEST_CASE("Object motion uses vanilla relative speed bounds") {
    CHECK(acceptsMotion({1.5f,0,0},{-100,0,0}));
    CHECK(acceptsMotion({50,0,0},{50,0,0}));
    CHECK(acceptsMotion({10,0,0},{4.75f,0,0}));
    CHECK_FALSE(acceptsMotion({10,0,0},{4.74f,0,0}));
    CHECK_FALSE(acceptsMotion({NAN,0,0},{0,0,0}));
}

TEST_CASE("Moving anchors preserve local coordinates through rotation and translation") {
    Matrix34 m{{{0,0,1,100},{0,1,0,20},{-1,0,0,30}}};
    REQUIRE(m.valid());
    const Vec3 point{2,3,4};
    const auto world=m.point(point), local=m.local(world);
    CHECK(world.x==104); CHECK(world.y==23); CHECK(world.z==28);
    CHECK(local.x==2); CHECK(local.y==3); CHECK(local.z==4);
    m.m[0][0]=NAN; CHECK_FALSE(m.valid());
    m=Matrix34{}; m.m[0][0]=2; CHECK_FALSE(m.valid());
}
