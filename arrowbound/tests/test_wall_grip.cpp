// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <array>
#include <cstring>
#include <doctest.h>

#include "WallGrip.hpp"
#include "../src/program/modules/arrowbound/WallGripService.hpp"
#include "ActionContext.hpp"
#include "AimRaycaster.hpp"
#include "HookshotInput.hpp"
#include "HookshotWorld.hpp"

using namespace arrowbound;
using namespace arrowbound::pure;
namespace {
struct GripHarness;
GripHarness* current = nullptr;
struct GripHarness {
    HookshotRuntime rt{};
    Vec3 position{0, 10, -1.25f};
    Vec3 impact{0, 10, 0};
    Vec3 velocity{}, posePosition{}, rayFrom{}, rayTo{};
    float pose[9]{};
    int poseWrites = 0, velocityWrites = 0, abandoned = 0;
    bool rayAvailable = true;
    GripHarness() {
        current = this;
        rt.session.tick = 100;
        rt.session.worldGen = 7;
        rt.arrowTrip.shotSeqSeen = 12;
        rt.grip.hooksReady.store(1);
        rt.grip.npadValid.store(1);
        rt.drive.parasailActive.store(1);
        rt.drive.presentParaglider.store(1);
        rt.arrow.playerActor.store(0x1000);
    }
    ~GripHarness() { current = nullptr; }
    TargetSample surface() const {
        TargetSample sample{};
        sample.completed = sample.hit = sample.hitBodyKnown = true;
        sample.position = impact;
        sample.normal = {0, 0, -1};
        sample.rayDirection = {0, 0, 1};
        sample.generation = rt.session.worldGen;
        sample.sequence = rt.arrowTrip.wall.request;
        return sample;
    }
    void request() {
        REQUIRE(wall_grip::begin(rt, impact, {0, 0, 1}));
        CHECK(wall_grip::service(rt) == nullptr);
        CHECK(rt.arrowTrip.phase == ArrowPhase::WallProbe);
    }
    void confirm() {
        ++rt.session.tick;
        rt.aim.sample = surface();
        rt.aim.sampleTick = rt.session.tick;
        CHECK(wall_grip::service(rt) == nullptr);
        CHECK(rt.arrowTrip.phase == ArrowPhase::WallGrip);
    }
};
}

namespace arrowbound {
HookshotRuntime& runtime() { return current->rt; }
}
namespace arrowbound::world {
Vec3 playerPosition() { return current->position; }
bool readPlayerRotation(float out[9]) {
    // Starts backward: yaw must change, not merely pass an already-correct fixture.
    const float basis[9]{-1, 0, 0, 0, 1, 0, 0, 0, -1};
    std::memcpy(out, basis, sizeof(basis));
    return true;
}
bool forcePlayerPose(const float rotation[9], const Vec3& position) {
    ++current->poseWrites;
    std::memcpy(current->pose, rotation, sizeof(current->pose));
    current->posePosition = position;
    return true;
}
}
namespace arrowbound::aim {
bool request(const Vec3& from, const Vec3& to, const Vec3&, const Vec3&,
             std::uint32_t& sequence, std::uint32_t, std::uint64_t, const engine::RayGroup*) {
    if (!current->rayAvailable) return false;
    current->rayFrom = from;
    current->rayTo = to;
    ++sequence;
    return true;
}
void abandonPending() { ++current->abandoned; }
}
namespace arrowbound::action {
bool Context::actorOk() const { return actor == 0x1000; }
bool Context::playerOk() const { return playerComponent == 0x2000; }
Context resolve(void* object) { return object ? Context{0x1000, 0x2000} : Context{}; }
Vec3 actorPosition(const Context&) { return current->position; }
void setLinearVelocity(const Context&, const Vec3& value) {
    ++current->velocityWrites;
    current->velocity = value;
}
}

TEST_CASE("impact grip verifies surface before turning or providing any motion") {
    GripHarness h;
    h.request();
    CHECK(h.poseWrites == 0);
    CHECK(h.velocityWrites == 0);
    CHECK(h.rayFrom.z == doctest::Approx(-0.5f));
    CHECK(h.rayTo.z == doctest::Approx(0.5f));
    h.confirm();
    CHECK(h.poseWrites == 1);
    CHECK(distance(h.posePosition, h.position) == 0);
    CHECK(h.pose[8] == doctest::Approx(1));
    CHECK(h.pose[4] == doctest::Approx(1));
    wall_grip::onParasailUpdate(&h);
    CHECK(h.velocityWrites == 1);
    CHECK(length(h.velocity) == doctest::Approx(4));
    CHECK(h.velocity.z > 0);
    CHECK(h.velocity.y < 0);
    CHECK(h.poseWrites == 1); // No position carrier during physical approach.
}

TEST_CASE("wall surface gate refuses unsupported geometry and stale request identity") {
    GripHarness h;
    h.request();
    const auto good = h.surface();
    const auto check = [&](TargetSample s, Vec3 link = Vec3{0, 10, -1.25f}) {
        return wallGripSurface(s, h.impact, link, 7, good.sequence);
    };
    CHECK(check(good) == Verdict::Valid);
    auto s = good; s.shapeFlags = 1; CHECK(check(s) == Verdict::NoClimb);
    s = good; s.hitBodyKnown = false; CHECK(check(s) == Verdict::MovingOrUnsupported);
    for (unsigned motion : {1u, 2u}) {
        s = good; s.hitMotionType = motion; CHECK(check(s) == Verdict::MovingOrUnsupported);
    }
    s = good; s.normal = {0, 1, 0}; s.rayDirection = {0, -1, 0};
    CHECK(check(s) == Verdict::Floor);
    s = good; s.normal = {0, -1, 0}; s.rayDirection = {0, 1, 0};
    CHECK(check(s) == Verdict::CeilingOrOverhang);
    s = good; s.normal.z = 1; CHECK(check(s) == Verdict::Backface);
    s = good; s.position.x = 0.51f; CHECK(check(s) == Verdict::BlockedCorridor);
    s = good; ++s.generation; CHECK(check(s) == Verdict::Pending);
    s = good; ++s.sequence; CHECK(check(s) == Verdict::Pending);
    s = good; s.normal.x = NAN; CHECK(check(s) == Verdict::InvalidFinite);
    s = good; s.hit = false; CHECK(check(s) == Verdict::Miss);
    CHECK(check(good, {0, 10, -4.01f}) == Verdict::TooFar);
    CHECK(check(good, {0, 10, 1}) == Verdict::Backface);
    CHECK(check(good, {NAN, 0, 0}) == Verdict::InvalidFinite);
}

TEST_CASE("grip yaw faces oblique wall normals without changing pitch or roll") {
    const float basis[9]{1, 0, 0, 0, 0.8f, -0.6f, 0, 0.6f, 0.8f};
    for (Vec3 normal : {Vec3{-1, 0, 0}, Vec3{0.6f, 0, -0.8f}, Vec3{0, 0, 1}}) {
        float out[9]{};
        REQUIRE(wallGripBasis(basis, normal, {}, out));
        CHECK(out[3] == basis[3]); CHECK(out[4] == basis[4]); CHECK(out[5] == basis[5]);
        const Vec3 forward{out[2], 0, out[8]};
        CHECK(dot(mul(forward, 1 / length(forward)), mul(normal, -1)) == doctest::Approx(1));
        CHECK(length({out[0], out[3], out[6]}) == doctest::Approx(1));
    }
}

TEST_CASE("native climb releases velocity presentation and processed input before its update") {
    GripHarness h;
    h.request(); h.confirm();
    alignas(16) std::array<unsigned char, 0x188> bytes{};
    const std::uint64_t sample = 23;
    std::memcpy(bytes.data() + 0x180, &sample, sizeof(sample));
    auto* stick = reinterpret_cast<float*>(bytes.data() + 0x120);
    stick[0] = 0.4f; stick[1] = -0.6f;
    wall_grip::onControllerUpdate(bytes.data());
    CHECK(stick[0] == 0); CHECK(stick[1] == 1);
    wall_grip::onClimbUpdate(&h);
    CHECK(h.rt.drive.captureActive.load() == 0);
    CHECK(h.rt.drive.presentParaglider.load() == 0);
    CHECK(h.rt.drive.forceEntry.load() == 0);
    CHECK(h.rt.drive.admissionWanted.load() == 0);
    CHECK(h.rt.grip.armed.load() == 0);
    wall_grip::onParasailUpdate(&h);
    CHECK(h.velocityWrites == 0);
    stick[0] = 0.3f; stick[1] = -0.2f;
    wall_grip::onControllerUpdate(bytes.data());
    CHECK(stick[0] == 0.3f); CHECK(stick[1] == -0.2f);
    CHECK(std::strcmp(wall_grip::service(h.rt), "climb") == 0);
}

TEST_CASE("all disengagement paths can stop both grip writers and in-flight confirmation") {
    for (bool confirmed : {false, true}) {
        GripHarness h;
        h.request();
        if (confirmed) h.confirm();
        wall_grip::stop(h.rt); // Shared by B, re-aim, disable, reset and failures.
        CHECK(h.rt.drive.captureActive.load() == 0);
        CHECK(h.rt.grip.armed.load() == 0);
        CHECK(h.abandoned == 1);
        wall_grip::onParasailUpdate(&h);
        CHECK(h.velocityWrites == 0);
        wall_grip::stop(h.rt);
        CHECK(h.abandoned == 1);
    }
    for (auto phase : {ArrowPhase::WallProbe, ArrowPhase::WallGrip}) {
        ArrowInputOwnership input;
        CHECK(input.update(phase, false, false, true).reaim);
        const auto cancel = input.update(phase, true, true, false);
        CHECK(cancel.cancel); CHECK(cancel.consumeB);
    }
}

TEST_CASE("wall intent is applied only to the owned controller while grip is active") {
    CHECK(wallGripOwnsInput(true, true, 0, 0, 1));
    CHECK_FALSE(wallGripOwnsInput(false, true, 0, 0, 1));
    CHECK_FALSE(wallGripOwnsInput(true, false, 0, 0, 1));
    CHECK_FALSE(wallGripOwnsInput(true, true, 0, 1, 1));
    CHECK_FALSE(wallGripOwnsInput(true, true, 0, 0, 0));
    GripHarness h;
    h.request(); h.confirm();
    alignas(16) std::array<unsigned char, 0x188> bytes{};
    auto* stick = reinterpret_cast<float*>(bytes.data() + 0x120);
    stick[0] = 0.25f; stick[1] = -0.75f;
    const std::uint32_t wrongId = 1;
    const std::uint64_t sample = 12;
    std::memcpy(bytes.data() + 0x178, &wrongId, sizeof(wrongId));
    std::memcpy(bytes.data() + 0x180, &sample, sizeof(sample));
    wall_grip::onControllerUpdate(bytes.data());
    CHECK(stick[0] == 0.25f); CHECK(stick[1] == -0.75f);
    const std::uint32_t correctId = 0;
    const std::uint64_t emptySample = 0;
    std::memcpy(bytes.data() + 0x178, &correctId, sizeof(correctId));
    std::memcpy(bytes.data() + 0x180, &emptySample, sizeof(emptySample));
    wall_grip::onControllerUpdate(bytes.data());
    CHECK(stick[0] == 0.25f); CHECK(stick[1] == -0.75f);
}

TEST_CASE("retired impact confirmation cannot attach a newly fired arrow to the old wall") {
    GripHarness h;
    h.request();
    const auto oldSample = h.surface();
    wall_grip::stop(h.rt);
    retireArrowTrip(h.rt.arrowTrip, 13);
    h.request();
    REQUIRE(h.rt.arrowTrip.wall.request != oldSample.sequence);
    ++h.rt.session.tick;
    h.rt.aim.sample = oldSample;
    h.rt.aim.sampleTick = h.rt.session.tick;
    CHECK(wall_grip::service(h.rt) == nullptr);
    CHECK(h.rt.arrowTrip.phase == ArrowPhase::WallProbe);
    CHECK(h.rt.drive.captureActive.load() == 0);
    CHECK(h.poseWrites == 0);
    h.confirm();
    CHECK(h.rt.drive.captureActive.load() == 1);
}

TEST_CASE("climb acquired while confirming a wall wins without re-opening the glider") {
    GripHarness h;
    h.request();
    wall_grip::onClimbUpdate(&h);
    CHECK(std::strcmp(wall_grip::service(h.rt), "climb") == 0);
    CHECK(h.rt.drive.captureActive.load() == 0);
    CHECK(h.rt.drive.presentParaglider.load() == 0);
    CHECK(h.poseWrites == 0);
    CHECK(h.velocityWrites == 0);
}

TEST_CASE("native climb wins over timeouts and the glider-leave grace remains bounded") {
    WallGripWatch watch{100, 8, 10};
    CHECK(watch.update(190, 9, true, false, 10) == GripProgress::Acquired);
    CHECK(watch.update(190, 8, false, true, 11) == GripProgress::TimedOut);
    watch = {100, 8, 10};
    for (int i = 0; i < 15; ++i)
        CHECK(watch.update(101 + i, 8, false, false, 10) == GripProgress::Waiting);
    CHECK(watch.update(116, 8, false, false, 10) == GripProgress::LeftGlider);
    watch = {100, 8, 10};
    for (int i = 0; i < 10; ++i)
        CHECK(watch.update(101 + i, 8, false, true, 10) == GripProgress::Waiting);
    CHECK(watch.update(111, 8, false, true, 10) == GripProgress::Stalled);
    watch = {100, 8, 10};
    CHECK(watch.update(101, 8, true, true, 11) == GripProgress::Rejected);
}

TEST_CASE("missing hooks delayed rays and unsupported impact never arm a physical grip") {
    GripHarness h;
    h.rt.grip.hooksReady.store(0);
    CHECK_FALSE(wall_grip::begin(h.rt, h.impact, {0, 0, 1}));
    h.rt.grip.hooksReady.store(1);
    CHECK_FALSE(wall_grip::begin(h.rt, h.impact, {}));
    h.rayAvailable = false;
    h.request();
    h.rt.session.tick += 25;
    CHECK(std::strcmp(wall_grip::service(h.rt), "wall confirmation timeout") == 0);
    CHECK(h.poseWrites == 0); CHECK(h.rt.drive.captureActive.load() == 0);
    wall_grip::stop(h.rt);
    h.rayAvailable = true;
    h.request();
    ++h.rt.session.tick;
    h.rt.aim.sample = h.surface();
    h.rt.aim.sample.shapeFlags = 1;
    h.rt.aim.sampleTick = h.rt.session.tick;
    CHECK(std::strcmp(wall_grip::service(h.rt), "wall surface refused") == 0);
    CHECK(h.poseWrites == 0); CHECK(h.rt.drive.captureActive.load() == 0);
}

TEST_CASE("physical grip refuses a Link position that crossed the wall or left the envelope") {
    for (Vec3 position : {Vec3{0, 10, 0.1f}, Vec3{0, 10, -4.1f}}) {
        GripHarness h;
        h.request(); h.confirm();
        h.position = position;
        wall_grip::onParasailUpdate(&h);
        CHECK(h.rt.grip.rejected.load() == 1);
        CHECK(h.rt.drive.captureActive.load() == 0);
        CHECK(h.velocityWrites == 0);
    }
}
