#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include "doctest.h"
#include "TestPlatform.hpp"
#include "engine/LinkedStickOffsets.hpp"
#include "feature/FormationAssist.hpp"
#include "feature/RiderInputDiagnostics.hpp"
#include "pure/AssemblyContinuity.hpp"

namespace e = linked_stick::engine;
namespace f = linked_stick::feature;
namespace m = linked_stick::pure::matched;
namespace p = test_platform;
namespace {
std::array<e::AssemblyMotionSnapshot, 2> live;
bool worldReady = true, delivered = true;
unsigned writes = 0;
unsigned collisionBodies = 0, collisionFinishes = 0;
m::Vec lastLinear{}, lastAngular{};
std::uintptr_t writtenBody = 0;

e::AssemblyMotionSnapshot bike(unsigned id) {
    e::AssemblyMotionSnapshot s;
    s.receiver = 0x1000 + id * 0x100;
    s.integrator = 0x4000 + id * 0x100;
    s.memberCount = s.shape.count = 3;
    s.status = e::AssemblyMotionStatus::Ready;
    s.motion.position = {id * 8.0f, 0, 0};
    for (unsigned i = 0; i < 3; ++i) {
        auto& a = s.members[i];
        a.actor = 0x8000 + id * 0x100 + i * 8;
        a.nameIdentity = 0x10001 + i;
        a.rigidBody = 0x20000 + id * 0x100 + i * 8;
        a.shape = {i ? 2u : 1u, {0, 0, static_cast<float>(i)}, m::identity};
        s.shape.members[i] = a.shape;
    }
    return s;
}
struct Flight {
    f::FormationAssist assist;
    f::FleetTelemetryEndpoints endpoints;
    alignas(8) std::array<std::byte, 0xD0> framework{};
    template <class T> void field(std::ptrdiff_t offset, T value) {
        std::memcpy(framework.data() + offset, &value, sizeof(value));
    }
    Flight() {
        p::now = 1000000; p::logs.clear(); writes = 0; worldReady = delivered = true;
        collisionBodies = collisionFinishes = 0;
        live = {bike(0), bike(1)};
        endpoints.controllerReceiver = live[0].receiver;
        endpoints.receiverComponents[0] = live[1].receiver;
        endpoints.controllerActive = true; endpoints.receiverCount = 1;
        field(e::offsets::kPhysicsStepSeconds, 1.0f / 30);
        field(e::offsets::kPhysicsMode, 5u);
        field(e::offsets::kPhysicsModeValid, std::uint8_t{1});
        field(e::offsets::kPhysicsWorldIndexValid, std::uint8_t{1});
        assist.configure(0x7100000000);
    }
    bool step(bool enabled = true) {
        p::now += 33333;
        const bool accepted = assist.beginPhysics(reinterpret_cast<std::uintptr_t>(framework.data()), endpoints, enabled);
        if (accepted) assist.endPhysics();
        assist.heartbeat(endpoints, enabled);
        return accepted;
    }
};
void input(f::RiderInputDiagnostics& d, std::uint64_t tick, unsigned endpoint, float steering) {
    std::array<std::byte, linked_stick::pure::kRiderInputRecordSize> record{};
    const float forward = 0.75f;
    std::memcpy(record.data() + linked_stick::pure::kRiderInputForwardOffset, &forward, sizeof(float));
    std::memcpy(record.data() + linked_stick::pure::kRiderInputSteeringOffset, &steering, sizeof(float));
    f::RiderInputCopyToken token;
    token.tick = tick; token.endpoint = static_cast<std::uint8_t>(endpoint);
    token.redirected = endpoint != 0;
    d.recordCopy(token, record.data());
}
}

// Native seams are fake; formation lifecycle and logging use production code.
namespace linked_stick::engine {
void AssemblyMotionService::configure(std::uintptr_t) {}
bool AssemblyMotionService::beginPhysics(std::uintptr_t) { return worldReady; }
void AssemblyMotionService::beginCollisionFrame() { collisionBodies = 0; }
void AssemblyMotionService::allowObstaclePassThrough(const AssemblyMotionSnapshot& s) {
    CHECK(s.receiver == live[1].receiver);
    collisionBodies += s.memberCount;
}
void AssemblyMotionService::finishCollisionFrame(bool) { ++collisionFinishes; }
void AssemblyMotionService::verifyCollisionDelivery(bool) const {}
AssemblyMotionSnapshot AssemblyMotionService::capture(std::uintptr_t receiver) const {
    for (const auto& s : live) if (s.receiver == receiver) return s;
    return {};
}
AssemblyMotionStatus AssemblyMotionService::applyCorrection(AssemblyMotionSnapshot& s,
                                                            m::Vec dv, m::Vec dw) const {
    ++writes; lastLinear = dv; lastAngular = dw; writtenBody = s.members[0].rigidBody;
    return AssemblyMotionStatus::Ready;
}
bool AssemblyMotionService::verifyDelivery(const AssemblyMotionSnapshot&, float& v, float& w) const {
    v = w = delivered ? 0.0f : 1.0f;
    return delivered;
}
void AssemblyMotionService::logDeliveryEvidence(const AssemblyMotionSnapshot&, unsigned) const {}
const char* assemblyMotionStatusName(AssemblyMotionStatus s) {
    return s == AssemblyMotionStatus::Ready ? "ready" : "body";
}
}

TEST_CASE("production formation tolerates startup handle/order/body changes without recapturing lane") {
    Flight flight;
    REQUIRE(flight.step()); REQUIRE(writes == 1);
    live[0].integrator += 0x1000;
    live[1].integrator += 0x1000;
    std::swap(live[0].members[0], live[0].members[2]);
    live[1].members[0].rigidBody += 0x1000;
    live[1].motion.position.y = 5;
    live[0].motion.angular = {0, 0.4f, 0};
    REQUIRE(flight.step());
    CHECK(writes == 2);
    CHECK(writtenBody == live[1].members[0].rigidBody);
    CHECK(lastLinear.y < 0);
    CHECK(lastAngular.y > 0);
    CHECK(flight.assist.diagnostics().heightError == doctest::Approx(5));
    CHECK(flight.assist.diagnostics().measurementValid);
    CHECK(p::contains("CONTINUITY endpoint=0 safe=1"));
    CHECK(p::contains("body_changed=1"));
}
TEST_CASE("production collision admission ends on dismount disable clear and construction refusal") {
    Flight flight;
    flight.step();
    REQUIRE(collisionBodies == 3);
    bool enabled = true;
    SUBCASE("dismount") { flight.endpoints.controllerActive = false; }
    SUBCASE("disable assist") { enabled = false; }
    SUBCASE("clear") { flight.assist.onPairCleared(); flight.endpoints.receiverCount = 0; }
    SUBCASE("construction changed") { ++live[1].members[1].actor; }
    SUBCASE("invalid controller") { live[0].status = e::AssemblyMotionStatus::PhysicsBodyUnavailable; }
    flight.step(enabled);
    CHECK(collisionBodies == 0);
    CHECK(collisionFinishes == 2);
}
TEST_CASE("production keeps issuing correction beyond the old separation and relative speed cutoffs") {
    Flight flight;
    flight.step();
    live[1].motion.position = {160, 30, 0};
    live[1].motion.velocity = {40, 0, 0};
    live[1].motion.rotation = {-1,0,0,0,1,0,0,0,-1};
    for (int i = 0; i < 120; ++i) flight.step();
    CHECK(writes == 121);
    CHECK(flight.assist.diagnostics().measurementValid);
    CHECK(lastLinear.x < -1);
    CHECK(lastLinear.y < -1);
    CHECK(m::length(lastAngular) > 0.26f);
    CHECK_FALSE(p::contains("separated:"));
}
TEST_CASE("production formation refuses topology changes and invalidates stale measurements") {
    Flight flight;
    flight.step();
    SUBCASE("equal count actor replacement") { live[1].members[1].actor += 0x1000; }
    SUBCASE("same actors changed geometry") { live[1].members[1].shape.position.x += 0.5f; }
    SUBCASE("member detached") { --live[1].memberCount; }
    SUBCASE("identity changed") { ++live[1].members[1].nameIdentity; }
    SUBCASE("duplicate body") { live[1].members[1].rigidBody = live[1].members[2].rigidBody; }
    flight.step();
    CHECK(writes == 1);
    CHECK_FALSE(flight.assist.diagnostics().measurementValid);
    CHECK(p::contains("CONTINUITY endpoint=1 safe=0"));
    CHECK(p::contains("role=original")); CHECK(p::contains("role=current"));
    live[1] = bike(1);
    flight.step();
    CHECK(writes == 1);
    CHECK(p::contains("STATE endpoint=1 state=construction changed"));
    flight.assist.onPairPublished(); flight.step(); CHECK(writes == 2);
}
TEST_CASE("production refusal telemetry survives an unfinished run and capture failure") {
    Flight flight;
    flight.step();
    live[0].status = e::AssemblyMotionStatus::PhysicsBodyUnavailable;
    p::logs.clear();
    for (int i = 0; i < 70; ++i) flight.step();
    CHECK(writes == 1);
    CHECK_FALSE(flight.assist.diagnostics().measurementValid);
    CHECK(p::contains("capture-refused"));
    CHECK(p::contains("PROGRESS active=1"));
    CHECK(p::contains("HEARTBEAT enabled=1 active=1"));
    CHECK_FALSE(p::contains("[matched] END"));
}
TEST_CASE("production diagnostics expire measurements when physics stops and logs excluded passes") {
    Flight flight;
    flight.step();
    flight.field(e::offsets::kPhysicsMode, 4u);
    p::now += 2000000;
    CHECK_FALSE(flight.step());
    CHECK_FALSE(flight.assist.diagnostics().measurementValid);
    CHECK(std::string(flight.assist.diagnostics().status) == "physics paused/unavailable");
    CHECK(p::contains("mode_reject=1"));
    CHECK(p::contains("mode=4 mode_valid=1"));
}
TEST_CASE("production reset invalidates measurements before the next physics pass") {
    Flight flight;
    flight.step();
    REQUIRE(flight.assist.diagnostics().measurementValid);
    flight.assist.onPairCleared();
    CHECK_FALSE(flight.assist.diagnostics().measurementValid);
}
TEST_CASE("production physics failures remain logged without any successful correction") {
    Flight flight;
    worldReady = false;
    for (int i = 0; i < 70; ++i) flight.step();
    CHECK(writes == 0);
    CHECK(p::contains("BODY endpoint=0"));
    CHECK(p::contains("state=physics unavailable"));
    CHECK(p::contains("requested=0"));
    CHECK_FALSE(flight.assist.diagnostics().measurementValid);
}
TEST_CASE("production input diagnostics emit periodic evidence and steering before run completion") {
    p::now = 1000000; p::logs.clear();
    f::RiderInputDiagnostics d;
    d.enter(); d.onPairPublished(1, 1);
    for (std::uint64_t tick = 1; tick <= 80; ++tick) {
        const float steering = tick < 20 ? 0 : 0.65f;
        input(d, tick, 0, steering);
        input(d, tick, 1, tick < 40 ? steering : 0);
        p::now += 33333;
        d.tick(tick + 1, 1, true);
    }
    CHECK(p::contains("ENDPOINT_SUMMARY run=1 endpoint=0"));
    CHECK(p::contains("ENDPOINT_SUMMARY run=1 endpoint=1"));
    CHECK(p::contains("reason=periodic paired=1 active=1"));
    CHECK(p::contains("steering_m=650"));
    CHECK(p::contains("guide_m=750,650 receiver_m=750,0"));
    CHECK(d.diagnostics().valueMismatchFrames > 0);
    CHECK_FALSE(p::contains("[input] END "));
    p::logs.clear();
    p::now += 2000000;
    d.tick(150, 1, true);
    CHECK(p::contains("reason=periodic"));
    CHECK(d.diagnostics().longestGapTicks > 1);
}
TEST_CASE("unpaired and inactive input telemetry still has a periodic heartbeat") {
    p::now = 1000000; p::logs.clear();
    f::RiderInputDiagnostics d;
    d.enter(); d.tick(1, 0, false);
    CHECK(p::contains("reason=periodic paired=0 active=0"));
    CHECK_FALSE(p::contains("[input] END "));
}
TEST_CASE("native getter axes retain values counts and ages independently of formation") {
    p::now = 1000000; p::logs.clear();
    f::FleetTelemetry fleet;
    fleet.enter();
    f::FleetTelemetryEndpoints endpoints;
    endpoints.receiverCount = 1; endpoints.controllerActive = true;
    fleet.recordInput(0, f::ControlAxis::X, 0.6f);
    fleet.recordInput(1, f::ControlAxis::X, 0.6f);
    fleet.tick(endpoints);
    CHECK(p::contains("AXES endpoint=1 active=1"));
    CHECK(p::contains("x_fb_y_m=600,0,0 calls=1,0,0"));
    p::logs.clear(); p::now += 2000000; fleet.tick(endpoints);
    CHECK(p::contains("calls=1,0,0 last_ticks=0,0,0"));
}
