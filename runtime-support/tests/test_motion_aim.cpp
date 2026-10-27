// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include "totk/engine/MotionCamera.hpp"
#include <limits>
using namespace totk::engine::motion;

namespace {
std::uint64_t clockAt(double seconds) { return std::uint64_t(seconds * 30 * 4294967296.0); }
Frame frame(double seconds) { return {1, 2, clockAt(seconds), true, true, false}; }
AimState turn(unsigned hz, float seconds = 1) {
    AimState state = advance({}, frame(0), {{}, true, true});
    for (unsigned i = 1; i <= unsigned(float(hz) * seconds); ++i)
        state = advance(state, frame(double(i)/hz), {{0.1f*kTau, -0.05f*kTau}, true, true});
    return state;
}
const View view{{0, 2, -5}, {0, 2, 0}, {0, 1, 0}, 1.0f};
const Vector pivot{0, 0, 0};
}

TEST_CASE("native deltas preserve travel across frame rates and use normal camera directions") {
    const auto reference = turn(30);
    CHECK(reference.target.yaw == doctest::Approx(-0.1f*kTau));
    CHECK(reference.target.pitch == doctest::Approx(-0.05f*kTau));
    CHECK(reference.angle.yaw < -0.60f);
    CHECK(reference.angle.pitch < -0.30f);
    for (const unsigned hz : {60u, 120u}) {
        const auto other = turn(hz);
        CHECK(other.target.yaw == doctest::Approx(reference.target.yaw).epsilon(0.00001));
        CHECK(other.target.pitch == doctest::Approx(reference.target.pitch).epsilon(0.00001));
    }
    const auto aimed = orbit(view, pivot, reference.angle);
    REQUIRE(aimed);
    const auto direction = subtract(aimed->at, aimed->eye);
    CHECK(direction[0] > 0);
    CHECK(direction[1] < 0);
}

TEST_CASE("native angular following halves the remaining offset each 30 Hz frame") {
    for (const unsigned hz : {30u, 60u, 120u}) {
        auto state = advance({}, frame(0), {{}, true, true});
        for (unsigned tick = 1; tick <= hz/10; ++tick) {
            state = advance(state, frame(double(tick)/hz), {{tick == 1 ? 0.8f*hz : 0, tick == 1 ? -0.4f*hz : 0}, true, true});
            const float followed = 1-std::pow(0.5f, float(tick)*30/hz);
            CHECK(state.angle.yaw == doctest::Approx(-0.8f*followed));
            CHECK(state.angle.pitch == doctest::Approx(-0.4f*followed));
        }
    }
}

TEST_CASE("each inversion option affects only new motion on its own axis") {
    const auto initial = advance({}, frame(0), {{}, true, true});
    for (const bool horizontal : {false, true}) {
        for (const bool vertical : {false, true}) {
            const auto state = advance(initial, frame(1.0/30), {{12,-6},true,true,horizontal,vertical});
            CHECK(state.angle.yaw == doctest::Approx(horizontal ? 0.2f : -0.2f));
            CHECK(state.angle.pitch == doctest::Approx(vertical ? 0.1f : -0.1f));
            const auto changed = advance(state, frame(2.0/30), {{},true,true,!horizontal,!vertical});
            CHECK(changed.target.yaw == state.target.yaw);
            CHECK(changed.target.pitch == state.target.pitch);
        }
    }
}

TEST_CASE("gyro orbit preserves distances field of view and native auxiliary data") {
    const auto rotated = orbit(view, pivot, {0.7f, 0.3f});
    REQUIRE(rotated);
    CHECK(length(rotated->eye) == doctest::Approx(length(view.eye)));
    CHECK(length(subtract(rotated->at, rotated->eye)) == doctest::Approx(5));
    CHECK(rotated->auxiliary == view.auxiliary);
    CHECK(rotated->fovy == view.fovy);
    auto opaque = view;
    opaque.auxiliary = {0, 0, 0};
    REQUIRE(orbit(opaque, pivot, {0.7f, 0.3f}));
    CHECK(orbit(opaque, pivot, {0.7f, 0.3f})->auxiliary == opaque.auxiliary);
}

TEST_CASE("duplicate clocks pauses wheel and stale samples cannot accumulate motion") {
    const auto state = turn(30);
    const Sample input{{0.1f, 0.05f}, true, true};
    CHECK(advance(state, frame(1), input).angle.yaw == state.angle.yaw);
    auto paused = frame(2);
    paused.running = false;
    const auto stopped = advance(state, paused, input);
    CHECK(stopped.angle.yaw == state.angle.yaw);
    CHECK(stopped.target.yaw == state.target.yaw);
    paused.running = true;
    paused.wheelOpen = true;
    CHECK(advance(state, paused, input).angle.yaw == state.angle.yaw);
    CHECK(advance(state, frame(2), {{1, 1}, true, false}).angle.yaw == state.angle.yaw);
    CHECK(advance(state, frame(2), input).angle.yaw == state.angle.yaw);
    CHECK(advance(state, frame(0), input).angle.yaw == state.angle.yaw);
}

TEST_CASE("new scene camera aim session and disabled motion reset the offset") {
    const auto state = turn(30);
    const Sample input{{0.1f, 0.05f}, true, true};
    for (unsigned change = 0; change < 4; ++change) {
        auto next = frame(1.03);
        if (change == 0) ++next.scene;
        if (change == 1) ++next.camera;
        if (change == 2) next.aiming = false;
        if (change == 3) next.scene = 0;
        CHECK(advance(state, next, input).angle.yaw == 0);
    }
    CHECK(advance(state, frame(1.03), {{1, 1}, false, true}).angle.yaw == 0);
}

TEST_CASE("native small motion survives while invalid deltas and extreme pitch stay bounded") {
    const auto clean = advance({}, frame(0), {{}, true, true});
    CHECK(advance(clean, frame(0.03), {{0.00001f, 0.00001f}, true, true}).angle.yaw < 0);
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(), 110.0f})
        CHECK(advance(clean, frame(0.03), {{bad, bad}, true, true}).angle.yaw == 0);
    const auto clamped = turn(30, 10);
    CHECK(clamped.target.pitch == -kPitchLimit);
    CHECK(clamped.angle.pitch == doctest::Approx(-kPitchLimit));
    CHECK(std::fabs(clamped.angle.yaw) <= kTau / 2);
    auto raised = view;
    raised.at[1] = 6;
    const auto result = orbit(raised, pivot, {0, 1.3f});
    REQUIRE(result);
    const auto direction = subtract(result->at, result->eye);
    CHECK(std::atan2(direction[1], std::hypot(direction[0], direction[2]))
          == doctest::Approx(kPitchLimit));
}

TEST_CASE("unusable camera views are refused without writing output") {
    auto bad = view;
    bad.at = bad.eye;
    CHECK_FALSE(orbit(bad, pivot, {1, 1}));
    bad = view;
    bad.eye[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(orbit(bad, pivot, {1, 1}));
}

TEST_CASE("chair yaw remains horizontal through a full revolution at different controller pitches") {
    for (const float tilt : {-0.7f, 0.0f, 0.7f, 1.2f}) {
        auto state = advance({}, frame(0), {{}, true, true});
        for (unsigned tick = 1; tick <= 240; ++tick) {
            const float heading = float(tick) * kTau / 240;
            const float c = std::cos(heading), s = std::sin(heading);
            const float t = std::cos(tilt), u = std::sin(tilt);
            // Controller-to-world R_y(heading) * R_x(tilt), stored by native preUpdate.
            const std::array<float,9> orientation{c,s*u,s*t, 0,t,-u, -s,c*u,c*t};
            const Vector up{orientation[3],orientation[4],orientation[5]};
            const Vector velocity{0, 0.125f*t, -0.125f*u};
            const auto rate = targetingRate(velocity, up);
            CHECK(rate.yaw == doctest::Approx(0.125f*kTau*1.65f));
            CHECK(rate.pitch == 0);
            const auto prior = state;
            state = advance(state, frame(double(tick)/30), {rate,true,true});
            CHECK(std::remainder(state.angle.yaw-prior.angle.yaw,kTau) < 0);
            const auto aimed = orbit(view,pivot,state.angle);
            REQUIRE(aimed);
            CHECK(aimed->at[1]-aimed->eye[1] == doctest::Approx(0));
        }
    }
}

TEST_CASE("controller pitch is independent of heading and inversion reverses only its intended axis") {
    const Vector up{0, 0.8f, 0.6f};
    const auto rate = targetingRate({0.1f,0.16f,0.12f},up);
    CHECK(rate.yaw == doctest::Approx(0.2f*kTau*1.65f));
    CHECK(rate.pitch == doctest::Approx(-0.1f*kTau*1.4f));
    const auto initial = advance({},frame(0),{{},true,true});
    for (const bool horizontal : {false,true}) for (const bool vertical : {false,true}) {
        const auto state = advance(initial,frame(1.0/30),{rate,true,true,horizontal,vertical});
        CHECK((state.angle.yaw > 0) == horizontal);
        CHECK((state.angle.pitch > 0) == vertical);
        const auto aimed = orbit(view,pivot,state.angle);
        REQUIRE(aimed);
        CHECK((aimed->at[1] > aimed->eye[1]) == vertical);
    }
}

TEST_CASE("native cache adapter reads both option changes and keeps sensor axes independent") {
    alignas(16) std::array<std::uint8_t,4096> component{};
    const auto camera = reinterpret_cast<std::uintptr_t>(component.data()+1096);
    auto current = frame(0);
    current.camera = camera;
    component[1096+1200] = 1;
    const Vector velocity{0.1f,0.16f,0.12f}, up{0,0.8f,0.6f};
    std::memcpy(component.data()+1096+1080,velocity.data(),sizeof(velocity));
    std::memcpy(component.data()+1096+1140,up.data(),sizeof(up));
    for (unsigned flags = 0; flags < 256; ++flags) {
        component[52] = static_cast<std::uint8_t>(flags);
        const auto input = sample(current);
        CHECK(input.permitted == bool(flags & 4));
        CHECK(input.invertHorizontal == bool(flags & 32));
        CHECK(input.invertVertical == bool(flags & 64));
        if (input.permitted) {
            CHECK(input.rate.yaw == doctest::Approx(0.2f*kTau*1.65f));
            CHECK(input.rate.pitch == doctest::Approx(-0.1f*kTau*1.4f));
        } else {
            CHECK(input.rate.yaw == 0);
            CHECK(input.rate.pitch == 0);
        }
    }
    component[52] = 4;
    component[1096+1221] = 1;
    CHECK_FALSE(sample(current).fresh);
    CHECK(sample(current).rate.yaw == 0);
    component[1096+1221] = 0;
    current.running = false;
    CHECK(sample(current).rate.pitch == 0);
    current.running = true;
    current.wheelOpen = true;
    CHECK(sample(current).rate.yaw == 0);
}
