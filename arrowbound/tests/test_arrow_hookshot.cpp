// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include <array>
#include <cstring>
#include <doctest.h>
#include <limits>

#include "../src/engine/ArrowImpactAdapter.hpp"
#include "ArrowHookshot.hpp"
#include "../src/program/modules/arrowbound/HookshotRuntime.hpp"

using namespace arrowbound::pure;

TEST_CASE("Link follows immediately at a fixed point behind the arrow") {
    const Vec3 arrow{20, 8, -4};
    Vec3 link{};
    REQUIRE(arrowTrailPoint(arrow, {3, 0, 0}, link));
    CHECK(link.x == doctest::Approx(18.75f));
    CHECK(link.y == arrow.y);
    CHECK(link.z == arrow.z);
    CHECK(distance(link, arrow) == doctest::Approx(ArrowConfig{}.trailDistance));

    REQUIRE(arrowTrailPoint(arrow, {0, 2, 0}, link));
    CHECK(link.y == doctest::Approx(6.75f));
    CHECK_FALSE(arrowTrailPoint(arrow, {}, link));
    CHECK_FALSE(arrowTrailPoint({NAN, 0, 0}, {1, 0, 0}, link));
}

TEST_CASE("level and steep paths have continuous forward travel at 30 and 60 Hz sampling") {
    for (const auto velocity : {Vec3{60, 0, 0}, Vec3{20, 150, 5}, Vec3{5, -180, 10}}) {
        for (const unsigned cadence : {1u, 2u}) {
            ArrowFollower follower;
            Vec3 previous{}, position{}, direction{};
            for (std::uint64_t tick = 0; tick < 120; ++tick) {
                const auto sampleTick = tick - tick % cadence;
                const Vec3 native = mul(velocity, static_cast<float>(sampleTick) / 60);
                REQUIRE(follower.update(tick, native, velocity, tick % cadence == 0,
                                        position, direction));
                CHECK(distance(position, mul(velocity, static_cast<float>(tick) / 60)) < 0.0002f);
                CHECK(dot(sub(position, previous), velocity) >= -0.0001f);
                previous = position;
            }
        }
    }
}

TEST_CASE("zero launch velocity waits without starting or poisoning the follower") {
    for (const auto launch : {Vec3{60, 0, 0}, Vec3{5, 150, 0}, Vec3{5, -180, 0}}) {
        ArrowFollower follower;
        bool following = false;
        Vec3 position{}, velocity{}, target{};
        unsigned accepted = 0;
        for (std::uint64_t tick = 400; tick < 410; ++tick) {
            const Vec3 nativeVelocity = tick < 402 ? Vec3{} : launch;
            const Vec3 nativePosition = add({10, 50, 20},
                mul(launch, static_cast<float>(tick > 402 ? tick - 402 : 0) / 60));
            // Same publication gate as onArrowSample: no mailbox event until motion is ready.
            if (!arrowMotionReady(nativePosition, nativeVelocity)) {
                CHECK_FALSE(following);
                continue;
            }
            following = true;
            ++accepted;
            REQUIRE(follower.update(tick, nativePosition, nativeVelocity, true, position, velocity));
            REQUIRE(arrowTrailPoint(position, velocity, target));
            CHECK(distance(target, position) == doctest::Approx(ArrowConfig{}.trailDistance));
        }
        CHECK(following);
        CHECK(accepted == 8);
    }
    CHECK_FALSE(arrowMotionReady({}, {}));
    CHECK_FALSE(arrowMotionReady({}, {0.001f, 0, 0}));
    CHECK_FALSE(arrowMotionReady({NAN, 0, 0}, {1, 0, 0}));
    CHECK_FALSE(arrowMotionReady({}, {INFINITY, 0, 0}));
    CHECK(arrowMotionReady({}, {0.01f, 0, 0}));
}

TEST_CASE("sampled velocity removes native time-rate compensation without changing its direction") {
    for (const Vec3 motion : {Vec3{132, 0, 0}, Vec3{15, 130, 2}, Vec3{5, -120, 10}}) {
        for (float rate : {0.0f, 1.0f, 10.0f, 4.88f, 2.38f, 0.1f}) {
            const float nativeRate = rate == 0 ? 1.0f : rate;
            const Vec3 raw = mul(motion, nativeRate);
            Vec3 converted{}, trail{};
            REQUIRE(arrowFollowVelocity(raw, rate, converted));
            CHECK(distance(converted, motion) < 0.0001f);
            REQUIRE(arrowTrailPoint({}, converted, trail));
            CHECK(distance(trail, mul(motion, -1.25f / length(motion))) < 0.0001f);
        }
    }
    Vec3 out{};
    CHECK(arrowFollowVelocity({}, 10, out)); // Startup motion-ready gate still rejects zero.
    CHECK_FALSE(arrowMotionReady({}, out));
    CHECK_FALSE(arrowFollowVelocity({132, 0, 0}, -1, out));
    CHECK_FALSE(arrowFollowVelocity({132, 0, 0}, NAN, out));
    CHECK_FALSE(arrowFollowVelocity({132, 0, 0}, INFINITY, out));
    CHECK_FALSE(arrowFollowVelocity({INFINITY, 0, 0}, 1, out));
    CHECK_FALSE(arrowFollowVelocity({132, 0, 0}, std::numeric_limits<float>::denorm_min(), out));
}

TEST_CASE("airborne time-rate recovery cannot launch the carrier ahead of the arrow") {
    // The compared fixtures vary only the native rate factor, not the real arrow path.
    constexpr std::array<float, 5> recovery{10.0f, 4.88f, 2.38f, 1.16f, 1.0f};
    for (const Vec3 motion : {Vec3{132, 0, 0}, Vec3{15, 130, 2}, Vec3{5, -120, 10}}) {
        for (unsigned cadence : {1u, 2u}) {
            ArrowFollower ground, airborne, unconverted;
            Vec3 groundPos{}, airPos{}, oldPos{}, direction{}, previous{};
            float oldMaxError = 0;
            for (std::uint64_t tick = 0; tick < 240; ++tick) {
                const auto sampleTick = tick - tick % cadence;
                const auto rateIndex = sampleTick / 2;
                const float rate = rateIndex < recovery.size() ? recovery[rateIndex] : 1;
                const Vec3 native = mul(motion, static_cast<float>(sampleTick) / 60);
                const Vec3 raw = mul(motion, rate);
                Vec3 converted{};
                REQUIRE(arrowFollowVelocity(raw, rate, converted));
                REQUIRE(ground.update(tick, native, motion, tick % cadence == 0, groundPos, direction));
                REQUIRE(airborne.update(tick, native, converted, tick % cadence == 0, airPos, direction));
                REQUIRE(unconverted.update(tick, native, raw, tick % cadence == 0, oldPos, direction));
                CHECK(distance(groundPos, airPos) < 0.001f);
                CHECK(dot(sub(airPos, previous), motion) >= 0);
                previous = airPos;
                const float error = distance(oldPos, groundPos);
                if (error > oldMaxError) oldMaxError = error;
            }
            CHECK(oldMaxError > 25); // Detects the old error; fixture is not a no-op.
        }
    }
}

TEST_CASE("duplicate and late samples never reset the carrier position") {
    ArrowFollower follower;
    Vec3 position{}, direction{};
    const Vec3 velocity{0, 180, 0};
    REQUIRE(follower.update(0, {}, velocity, true, position, direction));
    REQUIRE(follower.update(1, {}, velocity, true, position, direction));
    REQUIRE(follower.update(2, {0, 6, 0}, velocity, true, position, direction));
    REQUIRE(follower.update(3, {0, 6, 0}, velocity, true, position, direction));
    CHECK(position.y == doctest::Approx(9));
    REQUIRE(follower.update(4, {0, 12, 0}, velocity, true, position, direction));
    CHECK(position.y == doctest::Approx(12));
    for (std::uint64_t tick = 5; tick < 20; ++tick) {
        REQUIRE(follower.update(tick, {0, 12, 0}, velocity, false, position, direction));
        CHECK(position.y == doctest::Approx(static_cast<float>(tick) * 3));
    }
    CHECK_FALSE(follower.update(2, {}, velocity, true, position, direction));
    CHECK_FALSE(follower.update(21, {NAN, 0, 0}, velocity, true, position, direction));
}

TEST_CASE("the follower preserves native arc height through its apex") {
    ArrowFollower follower;
    Vec3 position{}, direction{};
    for (std::uint64_t tick = 0; tick < 180; ++tick) {
        const float t = static_cast<float>(tick - tick % 2) / 60;
        const Vec3 native{10 * t, 8 * t - 5 * t * t, 0};
        REQUIRE(follower.update(tick, native, {10, 8 - 10 * t, 0}, tick % 2 == 0,
                                position, direction));
        const float rendered = static_cast<float>(tick) / 60;
        CHECK(position.x == doctest::Approx(10 * rendered));
        CHECK(std::abs(position.y - (8 * rendered - 5 * rendered * rendered)) < 0.05f);
    }
}

TEST_CASE("cancel owns B through actual trip retirement until the button is released") {
    arrowbound::HookshotRuntime rt{};
    rt.arrowTrip.phase = ArrowPhase::Following;
    const auto cancel = rt.inputOwnership.update(rt.arrowTrip.phase, true, true, false);
    REQUIRE(cancel.cancel);
    CHECK(cancel.consumeB);
    CHECK_FALSE(cancel.reaim);
    rt.arrowTrip.phase = ArrowPhase::Bailout;
    CHECK(rt.inputOwnership.update(rt.arrowTrip.phase, true, false, false).consumeB);
    retireArrowTrip(rt.arrowTrip, 7);
    CHECK(cancel.consumeB); // The same frame's mask cannot be erased by retirement.
    const auto held = rt.inputOwnership.update(rt.arrowTrip.phase, true, false, false);
    CHECK(held.consumeB);
    CHECK_FALSE(held.cancel);
    CHECK_FALSE(rt.inputOwnership.update(rt.arrowTrip.phase, false, false, false).consumeB);
    const auto nextPress = rt.inputOwnership.update(rt.arrowTrip.phase, true, true, false);
    CHECK_FALSE(nextPress.cancel);
    CHECK_FALSE(nextPress.consumeB); // A separate B press closes ordinary gliding normally.
}

TEST_CASE("new bow draw detaches any owned flight but does not suppress normal aiming") {
    ArrowInputOwnership input;
    for (auto phase : {ArrowPhase::WaitingForArrow, ArrowPhase::Following, ArrowPhase::Bailout}) {
        const auto aim = input.update(phase, false, false, true);
        CHECK(aim.reaim);
        CHECK_FALSE(aim.cancel);
        CHECK_FALSE(aim.consumeB);
        CHECK_FALSE(input.update(phase, false, false, false).reaim);
    }
    CHECK_FALSE(input.update(ArrowPhase::Idle, false, false, true).reaim);
    const auto simultaneous = input.update(ArrowPhase::Following, true, true, true);
    CHECK(simultaneous.cancel);
    CHECK_FALSE(simultaneous.reaim);
}

TEST_CASE("bailout requires a glider update after cancellation rather than the stale active flag") {
    CHECK_FALSE(arrowBailoutReady(true, 90, 90));
    CHECK_FALSE(arrowBailoutReady(false, 91, 90));
    CHECK(arrowBailoutReady(true, 91, 90));
    CHECK(arrowBailoutReady(true, 0, 0xFFFFFFFFu));
}

TEST_CASE("only a newly created arrow can be claimed after retiring the old flight") {
    CHECK(arrowClaimIsNew(0));
    for (std::uint32_t state : {1u, 2u, 3u, 4u, 0xFFFFFFFFu})
        CHECK_FALSE(arrowClaimIsNew(state));
    arrowbound::ArrowTripState trip{};
    trip.phase = ArrowPhase::Following;
    trip.shotSeqSeen = 10;
    trip.sampleSeqSeen = 100;
    trip.hitSeqSeen = 20;
    trip.haveRequestedPosition = true;
    CHECK(arrowHasFlight(trip.phase, 10, trip.shotSeqSeen));
    retireArrowTrip(trip, 10);
    CHECK_FALSE(arrowHasFlight(trip.phase, 10, trip.shotSeqSeen));
    CHECK(arrowHasFlight(trip.phase, 11, trip.shotSeqSeen)); // Release not serviced yet.
    CHECK(trip.sampleSeqSeen == 0);
    CHECK(trip.hitSeqSeen == 0);
    CHECK_FALSE(trip.haveRequestedPosition);
}

TEST_CASE("bow completion is scoped to following and preserves native terminal results") {
    for (std::uint64_t result = 0; result < 4; ++result) {
        CHECK(arrowFlightBowResult(result, false) == result);
        CHECK(arrowFlightBowResult(result, true) == (result == 0 ? 2 : result));
    }
}

TEST_CASE("uneven repeated arrow samples do not stop or jump the continuous carrier") {
    for (const auto velocity : {Vec3{60, 0, 0}, Vec3{5, 150, 0}, Vec3{5, -180, 0}}) {
        ArrowFollower follower;
        Vec3 previous{}, position{}, direction{};
        const float nativeStep = length(velocity) / 60;
        for (std::uint64_t tick = 0; tick < 120; ++tick) {
            const auto sourceTick = tick - tick % 6;
            REQUIRE(follower.update(tick, mul(velocity, static_cast<float>(sourceTick) / 60),
                                    velocity, true, position, direction));
            if (tick > 8) {
                const Vec3 step = sub(position, previous);
                CHECK(dot(step, velocity) > 0);
                CHECK(length(step) >= nativeStep * 0.749f);
                CHECK(length(step) <= nativeStep * 1.251f);
            }
            previous = position;
        }
    }
}

TEST_CASE("late positions correct drift gradually without reversing or chasing Link's pose") {
    ArrowFollower follower;
    const Vec3 velocity{0, 60, 0};
    Vec3 position{}, direction{}, previous{};
    for (std::uint64_t tick = 0; tick < 240; ++tick) {
        const auto sourceTick = tick > 4 && tick < 90 ? tick - 4 : tick;
        Vec3 sample{0, static_cast<float>(sourceTick), 0};
        if (tick >= 30 && tick < 90) sample.x = 2;
        REQUIRE(follower.update(tick, sample, velocity, true, position, direction));
        if (tick) {
            CHECK(dot(sub(position, previous), velocity) > 0);
            CHECK(distance(position, previous) >= 0.749f);
            CHECK(distance(position, previous) <= 1.251f);
            CHECK(follower.correctionDistance() <= 0.2501f);
        }
        previous = position;
    }
    CHECK(distance(position, {0, 239, 0}) < 0.001f);
    REQUIRE(follower.update(239, {0, 239, 0}, velocity, true, position, direction));
    CHECK(distance(position, previous) == 0);
    REQUIRE(follower.update(242, {0, 242, 0}, velocity, true, position, direction, {}, 3.f/60));
    CHECK(distance(position, {0,242,0}) < 0.001f);
    ArrowFollower unstarted;
    CHECK_FALSE(unstarted.update(0, {}, velocity, false, position, direction));
    ArrowConfig invalid;
    invalid.followUpdatesPerSecond = 0;
    CHECK_FALSE(unstarted.update(0, {}, velocity, true, position, direction, invalid));
}

TEST_CASE("impact adapters preserve the arrow finishing point and direction") {
    std::array<unsigned char, 512> controller{};
    std::array<unsigned char, 64> motion{};
    const Vec3 direction{1, 0, 0}, settled{99, 20, 0}, adjusted{98, 20, 0};
    const float standoff = 2;
    std::memcpy(controller.data() + 276, &direction, sizeof(direction));
    std::memcpy(controller.data() + 288, &settled, sizeof(settled));
    std::memcpy(motion.data() + 56, &standoff, sizeof(standoff));
    const auto sensor = arrowbound::engine::nativeArrowImpact(
        controller.data(), ArrowImpactSource::Sensor, 6, false, &adjusted.x, motion.data());
    CHECK(sensor.contact.x == 100);
    CHECK(sensor.contact.y == 20);
    CHECK(sensor.direction.x == 1);

    std::memcpy(controller.data() + 288, &sensor.contact, sizeof(sensor.contact));
    const auto world = arrowbound::engine::nativeArrowImpact(
        controller.data(), ArrowImpactSource::WorldSweep, 0, false);
    CHECK(world.contact.x == sensor.contact.x);
    CHECK(world.contact.y == sensor.contact.y);
    Vec3 link{};
    REQUIRE(arrowTrailPoint(world.contact, world.direction, link));
    CHECK(link.x == doctest::Approx(98.75f));
}

TEST_CASE("the first actual impact ends live following without surface validation") {
    for (auto source : {ArrowImpactSource::Sensor, ArrowImpactSource::WorldSweep}) {
        std::atomic<std::uint32_t> published{0};
        unsigned calls = 0;
        ArrowImpact hit{source, {100, 20, 0}, {1, 0, 0}, 2, true};
        const auto send = [&](const ArrowImpact& value) {
            ++calls;
            CHECK(value.contact.x == 100);
        };
        CHECK(publishArrowImpact(false, hit, published, send) == ImpactPublication::NoHit);
        CHECK(publishArrowImpact(true, hit, published, send) == ImpactPublication::Published);
        CHECK(publishArrowImpact(true, hit, published, send) == ImpactPublication::Duplicate);
        CHECK(calls == 1);
    }

    std::atomic<std::uint32_t> published{0};
    ArrowImpact invalid{ArrowImpactSource::Sensor, {NAN, 0, 0}, {1, 0, 0}, 6, false};
    CHECK(publishArrowImpact(true, invalid, published, [](const ArrowImpact&) { FAIL("invalid hit"); }) ==
          ImpactPublication::Invalid);
    CHECK(published.load() == 0);
}

TEST_CASE("arrow follow and paraglider handoff timeouts are bounded") {
    CHECK_FALSE(arrowTimedOut(18, 0, 18));
    CHECK(arrowTimedOut(19, 0, 18));
    CHECK_FALSE(arrowTimedOut(100, 100, 18));
    CHECK(ArrowConfig{}.claimTimeoutTicks == 30);
    CHECK(ArrowConfig{}.updateTimeoutTicks == 18);
    CHECK(ArrowConfig{}.bailoutTimeoutTicks == 90);
}

TEST_CASE("steady glider entry is scoped to active arrow flight and preserves other bits") {
    for (const std::uint64_t native : {0ULL, 1ULL, 2ULL, 3ULL, 0xFFFFFFFFFFFFFFFEULL}) {
        CHECK(arrowFlightParasailEntry(native, false) == native);
        const auto following = arrowFlightParasailEntry(native, true);
        CHECK((following & 1ULL) == 1ULL);
        CHECK((following & ~1ULL) == (native & ~1ULL));
        CHECK(arrowFlightParasailEntry(following, true) == following);
    }
}

TEST_CASE("finishing or resetting a shot never replays its release") {
    struct Trip {
        ArrowPhase phase = ArrowPhase::Idle;
        std::uint32_t shotSeqSeen = 0;
        bool rotationValid = false;
        bool haveRequestedPosition = false;
    } trip;
    for (auto phase : {ArrowPhase::WaitingForArrow, ArrowPhase::Following, ArrowPhase::Bailout}) {
        trip = {phase, 12, true, true};
        retireArrowTrip(trip, 12);
        CHECK(trip.phase == ArrowPhase::Idle);
        CHECK(trip.shotSeqSeen == 12);
        CHECK_FALSE(trip.rotationValid);
        CHECK_FALSE(trip.haveRequestedPosition);
    }
}
