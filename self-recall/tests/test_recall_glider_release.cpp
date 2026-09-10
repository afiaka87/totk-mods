#include "RecallGliderPolicy.hpp"
#include "doctest.h"

using namespace self_recall::pure;

namespace {
struct Handoff {
    NativeTraversalState traversal;
    GliderRelease release;
    GliderReleaseContext player{0x1000, 7, 1, 100, true};
    std::uint64_t ticket() { return release.forceTicket(player.actor, player.actorId, traversal); }
    std::uint64_t arm(bool recorded = true) { return release.arm(player, recorded, traversal); }
};
}

TEST_CASE("native traversal observations match IDs including zero and cannot be cleared by foreign leaves") {
    NativeTraversalState native;
    CHECK_FALSE(native.falling(0));
    CHECK_FALSE(native.gliding(UINT32_MAX));
    CHECK_FALSE(native.climbing(0));
    native.enterFall(0);
    native.enterGlide(UINT32_MAX);
    native.enterClimb(0);
    native.leaveFall(8);
    native.leaveGlide(8);
    native.leaveClimb(8);
    CHECK(native.falling(0));
    CHECK(native.gliding(UINT32_MAX));
    CHECK(native.climbing(0));
    CHECK_FALSE(native.climbing(8));
    native.leaveFall(0);
    native.leaveGlide(UINT32_MAX);
    native.leaveClimb(0);
    CHECK_FALSE(native.falling(0));
    CHECK_FALSE(native.gliding(UINT32_MAX));
    CHECK_FALSE(native.climbing(0));
    native.enterFall(7);
    native.enterGlide(7);
    native.enterClimb(UINT32_MAX);
    native.clear();
    CHECK_FALSE(native.falling(7));
    CHECK_FALSE(native.gliding(7));
    CHECK_FALSE(native.climbing(UINT32_MAX));
}

TEST_CASE("release requires recorded native glide and a current native fall from the same player") {
    Handoff h;
    h.traversal.enterFall(h.player.actorId);
    CHECK(h.arm(false) == 0);
    CHECK(h.ticket() == 0);
    const auto serial = h.arm();
    REQUIRE(serial != 0);
    CHECK(h.ticket() == serial);
    CHECK(h.release.forceTicket(0x2000, h.player.actorId, h.traversal) == 0);
    CHECK(h.release.forceTicket(h.player.actor, 8, h.traversal) == 0);
    h.traversal.leaveFall(h.player.actorId);
    CHECK(h.ticket() == 0);
    h.traversal.enterFall(h.player.actorId);
    CHECK(h.ticket() == serial);
    h.traversal.enterGlide(h.player.actorId);
    CHECK(h.ticket() == 0);
    h.release.entered(h.player.actor, h.player.actorId);
    CHECK_FALSE(h.release.pending());
    CHECK(h.release.result().reason == GliderReleaseEnd::Entered);
    h.traversal.leaveGlide(h.player.actorId);
    CHECK(h.ticket() == 0); // Native close must never reopen the old request.
}

TEST_CASE("glider release is bounded to 45 input ticks and reports its admitted overrides") {
    Handoff h;
    h.traversal.enterFall(h.player.actorId);
    const auto serial = h.arm();
    REQUIRE(h.release.confirmForce(h.ticket()));
    h.player.tick += 44;
    h.release.service(h.player, false);
    REQUIRE(h.release.confirmForce(h.ticket()));
    h.player.tick += 1;
    h.release.service(h.player, false);
    CHECK(h.ticket() == 0);
    CHECK_FALSE(h.release.confirmForce(serial));
    CHECK(h.release.result().serial == serial);
    CHECK(h.release.result().reason == GliderReleaseEnd::TimedOut);
    CHECK(h.release.result().forcedCalls == 2);
}

TEST_CASE("invalid release context and already gliding do not arm a forced transition") {
    Handoff h;
    SUBCASE("missing actor") { h.player.actor = 0; }
    SUBCASE("missing generation") { h.player.worldGeneration = 0; }
    SUBCASE("unsafe") { h.player.allowed = false; }
    SUBCASE("tick addition would wrap") { h.player.tick = UINT64_MAX - GliderRelease::kAcquireTicks + 1; }
    SUBCASE("native glider is already open") { h.traversal.enterGlide(h.player.actorId); }
    CHECK(h.arm() == 0);
    CHECK_FALSE(h.release.pending());
}

TEST_CASE("changed player generation safety and backwards clock invalidate a pending glider release") {
    Handoff h;
    REQUIRE(h.arm() != 0);
    SUBCASE("recycled address") { ++h.player.actorId; }
    SUBCASE("different address") { ++h.player.actor; }
    SUBCASE("world generation") { ++h.player.worldGeneration; }
    SUBCASE("unsafe state") { h.player.allowed = false; }
    SUBCASE("clock reset") { --h.player.tick; }
    h.release.service(h.player, false);
    CHECK_FALSE(h.release.pending());
    CHECK(h.release.result().reason == GliderReleaseEnd::ContextChanged);
}

TEST_CASE("late selector result cannot authorize or cancel a replacement request") {
    Handoff h;
    h.traversal.enterFall(h.player.actorId);
    const auto old = h.arm();
    REQUIRE(old != 0);
    REQUIRE(h.ticket() == old);
    h.release.cancel(GliderReleaseEnd::NewRecall);
    const auto current = h.arm();
    REQUIRE(current > old);
    CHECK_FALSE(h.release.confirmForce(old));
    h.release.cancelTicket(old, GliderReleaseEnd::Unavailable);
    CHECK(h.ticket() == current);
    CHECK(h.release.confirmForce(current));
    h.release.entered(0x2000, h.player.actorId);
    h.release.entered(h.player.actor, 8);
    CHECK(h.release.pending());
    h.release.entered(h.player.actor, h.player.actorId);
    CHECK(h.release.result().serial == current);
    CHECK(h.release.result().reason == GliderReleaseEnd::Entered);
    CHECK(h.release.result().forcedCalls == 1);
    h.release.cancelTicket(old, GliderReleaseEnd::TimedOut);
    CHECK(h.release.result().serial == current);
}

TEST_CASE("a later user cancel and lost paraglider ownership end acquisition permanently") {
    Handoff h;
    h.traversal.enterFall(h.player.actorId);
    const auto serial = h.arm();
    REQUIRE(serial != 0);
    SUBCASE("user cancel") {
        h.release.service(h.player, true);
        CHECK(h.release.result().reason == GliderReleaseEnd::Cancelled);
    }
    SUBCASE("native acquisition gate failed") {
        h.release.cancelTicket(h.ticket(), GliderReleaseEnd::Unavailable);
        CHECK(h.release.result().reason == GliderReleaseEnd::Unavailable);
    }
    CHECK_FALSE(h.release.pending());
    CHECK(h.ticket() == 0);
    CHECK_FALSE(h.release.confirmForce(serial));
}
