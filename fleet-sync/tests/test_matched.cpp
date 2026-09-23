#include <limits>
#include <string>

#include "doctest.h"
#include "pure/MatchedFormation.hpp"

namespace m = linked_stick::pure::matched;

TEST_CASE("rough fleet row flattens minor forward and rearward offsets on both sides") {
    for (float side : {-1.0f, 1.0f}) for (float z : {-1.5f, 1.5f}) {
        m::Controller controller;
        m::Motion guide, receiver;
        receiver.position = {side * 8, 0, z};
        const auto first = controller.step(guide, receiver, 1.0f / 30, false);
        REQUIRE(first.apply);
        CHECK(first.capturedLane);
        CHECK(first.laneOffset.x == side * 8);
        CHECK(first.laneOffset.z == 0);
        CHECK(first.error.z == -z);
        receiver.position = {side * 8, 0, 0};
        CHECK(m::length(controller.step(guide, receiver, 1.0f / 30, false).error) < 0.0001f);
    }
}

TEST_CASE("delayed startup retains the original row rather than capturing a position ahead") {
    m::Controller controller;
    m::Motion guide, receiver;
    guide.radius = receiver.radius = 2;
    receiver.position = {4.14f, 0.2f, 0.65f};
    const auto waiting = controller.step(guide, receiver, 1.0f / 30, false);
    CHECK(waiting.state == m::State::TooClose);
    REQUIRE(waiting.capturedLane);
    CHECK(waiting.laneOffset.x == 5);
    CHECK(waiting.laneOffset.z == 0);
    receiver.position = {2, 2, 8};
    const auto started = controller.step(guide, receiver, 1.0f / 30, false);
    REQUIRE(started.apply);
    CHECK_FALSE(started.capturedLane);
    CHECK(started.error.x == 3);
    CHECK(started.error.z == -8);
}

TEST_CASE("deliberate front and rear placement remains authored for pairs and fleets") {
    for (bool pair : {false, true}) for (float z : {-20.0f, -5.0f, 5.0f, 20.0f}) {
        m::Controller controller;
        m::Motion guide, receiver;
        receiver.position = {0, 0, z};
        const auto c = controller.step(guide, receiver, 1.0f / 30, pair);
        REQUIRE(c.apply);
        CHECK(c.laneOffset.z == z);
        CHECK(c.laneOffset.x == 0);
        CHECK(m::length(c.error) == 0);
    }
}

TEST_CASE("row inference uses controller heading and does not recapture during a turn") {
    m::Controller controller;
    m::Motion guide, receiver;
    guide.rotation = {0, 0, 1, 0, 1, 0, -1, 0, 0};
    receiver.position = {1, 0, -8};
    auto first = controller.step(guide, receiver, 1.0f / 30, false);
    REQUIRE(first.apply);
    CHECK(first.laneOffset.x == 8);
    CHECK(first.laneOffset.z == 0);
    guide.rotation = m::identity;
    receiver.position = {8, 0, 0};
    const auto turned = controller.step(guide, receiver, 1.0f / 30, false);
    CHECK_FALSE(turned.capturedLane);
    CHECK(m::length(turned.error) == 0);
}

namespace {
m::Mat rotation(m::Vec axis, float angle) {
    axis = m::mul(axis, 1 / m::length(axis));
    const float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    const auto [x, y, z] = axis;
    return {t * x * x + c,     t * x * y - s * z, t * x * z + s * y,
            t * x * y + s * z, t * y * y + c,     t * y * z - s * x,
            t * x * z - s * y, t * y * z + s * x, t * z * z + c};
}
m::Shape bike() {
    m::Shape s{};
    s.count = 3;
    s.members[0] = {1, {0, 0, 0}, m::identity};
    s.members[1] = {2, {0, -0.5f, 1.2f}, rotation({1, 0, 0}, 0.785398f)};
    s.members[2] = {2, {0, -0.5f, -1.2f}, rotation({1, 0, 0}, 0.785398f)};
    return s;
}
}
TEST_CASE("matched build checks unordered geometry and fan orientation") {
    const auto a = bike();
    auto b = a;
    std::swap(b.members[0], b.members[2]);
    CHECK(m::sameShape(a, b));
    b.members[0].position.x += 0.5f;
    CHECK_FALSE(m::sameShape(a, b));
    b = a;
    b.members[1].rotation = rotation({1, 0, 0}, -0.785398f);
    CHECK_FALSE(m::sameShape(a, b));
    b = a;
    b.members[2] = b.members[1];
    CHECK_FALSE(m::sameShape(a, b));
    b = a;
    b.members[2].kind = 3;
    CHECK_FALSE(m::sameShape(a, b));
    b = a;
    b.members[2].position.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(m::sameShape(a, b));
    b = a;
    b.count = 22;
    CHECK_FALSE(m::sameShape(a, b));
}
TEST_CASE("attitude correction uses shortest world rotation through 180 degrees") {
    for (const auto axis : {m::Vec{1, 0, 0}, m::Vec{0, 1, 0}, m::Vec{0, 0, 1}}) {
        for (const float angle : {0.01f, 0.7f, 3.1415926f, -3.0f}) {
            const auto error = m::rotationError(rotation(axis, angle), m::identity);
            CHECK(m::length(error) == doctest::Approx(std::fabs(angle)).epsilon(0.0001));
            CHECK(m::dot(error, axis) == doctest::Approx(angle).epsilon(0.0001));
        }
    }
    auto reflected = m::identity;
    reflected[0] = -1;
    CHECK_FALSE(m::validRotation(reflected));
    auto scaled = m::identity;
    scaled[0] = 2;
    CHECK_FALSE(m::validRotation(scaled));
}
TEST_CASE("rigid member correction preserves tangential motion and queued velocity") {
    const m::Vec dv{0.04f, 0.02f, -0.03f}, dw{0, 0.1f, 0}, origin{20, 30, 40};
    const auto front = m::add(origin, {0, 0, 2});
    const auto back = m::add(origin, {0, 0, -2});
    const auto a = m::memberDelta(dv, dw, front, origin);
    const auto b = m::memberDelta(dv, dw, back, origin);
    CHECK(m::length(m::sub(m::mul(m::add(a, b), 0.5f), dv)) < 0.00001f);
    CHECK(a.x == doctest::Approx(0.24f));
    CHECK(b.x == doctest::Approx(-0.16f));
    // Nonzero motion deliberately differs from the rejected actor-zero fixture.
    const m::Vec queued{8, 2, -4};
    CHECK(m::add(queued, a).x == doctest::Approx(8.24f));
}
TEST_CASE("formation retains its lane and recovers instead of latching off at distance") {
    m::Controller controller;
    m::Motion guide, follower;
    follower.position = {8, 0, 0};
    auto command = controller.step(guide, follower, 1.0f / 60);
    CHECK(command.apply);
    CHECK(m::length(command.linear) == 0);
    CHECK(m::length(command.angular) == 0);
    follower.position.x = 7;
    command = controller.step(guide, follower, 1.0f / 60);
    CHECK(command.linear.x > 0);
    CHECK(m::length(command.linear) <= m::kCatchUpAcceleration / 60 + 0.00001f);
    follower.position = {80, 0, 0};
    command = controller.step(guide, follower, 1.0f / 60);
    CHECK(command.apply);
    CHECK(command.linear.x < 0);
    follower.position = {8, 0, 0};
    CHECK(controller.step(guide, follower, 1.0f / 60).apply);
    controller.reset();
    CHECK(controller.step(guide, follower, 1.0f / 60).apply);
    controller.reset();
    follower.position = {1, 0, 0};
    CHECK(controller.step(guide, follower, 1.0f / 60).state == m::State::TooClose);
    follower.position = {8, 0, 0};
    guide.velocity.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(controller.step(guide, follower, 1.0f / 60).apply);
}
TEST_CASE(
    "turning lane includes centripetal feedforward and height correction starts immediately") {
    m::Controller controller;
    m::Motion guide, follower;
    guide.angular = {0, 0.2f, 0};
    follower.angular = guide.angular;
    follower.position = {8, 0, 0};
    follower.velocity = {0, 0, -1.6f};
    auto c = controller.step(guide, follower, 0.02f);
    CHECK(c.linear.x == doctest::Approx(-0.32f * 0.02f));
    CHECK(c.vertical);
    guide.position.y = 2;
    follower.position.y = 2;
    for (int i = 0; i < 20; ++i) c = controller.step(guide, follower, 0.02f);
    CHECK(c.vertical);
    guide.position.y = 0;
    follower.position.y = 0;
    for (int i = 0; i < 20; ++i) c = controller.step(guide, follower, 0.02f);
    CHECK(c.vertical);
    CHECK(c.linear.y == 0);
    follower.velocity.y = 2;
    c = controller.step(guide, follower, 0.02f);
    CHECK(c.linear.y < -0.15f);
    CHECK_FALSE(controller.step(guide, follower, 1).apply);
}
TEST_CASE("bounded formation recovers attitude and spacing in a turning climbing synthetic plant") {
    for (const int hz : {30, 60, 120}) {
        const float dt = 1.0f / static_cast<float>(hz);
        m::Controller controller;
        m::Motion guide, follower;
        guide.velocity = {0, 2, 8};
        guide.angular = {0, 0.16f, 0};
        follower.position = {8, 0, 0};
        follower.velocity = guide.velocity;
        follower.angular = guide.angular;
        follower.rotation = rotation({0, 1, 0}, 0.9f);
        m::Command c{};
        for (int step = 0; step < hz * 40; ++step) {
            const auto previousVelocity = guide.velocity;
            guide.rotation = m::product(rotation({0, 1, 0}, guide.angular.y * dt), guide.rotation);
            guide.velocity = m::add(m::rotate(guide.rotation, {0, 0, 8}), {0, 2, 0});
            guide.position = m::add(guide.position, m::mul(guide.velocity, dt));
            c = controller.step(guide, follower, dt);
            REQUIRE(c.apply);
            CHECK(m::length(m::Vec{c.linear.x, 0, c.linear.z}) <= m::kTotalHorizontalAcceleration * dt + 0.00001f);
            CHECK(std::fabs(c.linear.y) <= m::kHeightAcceleration * dt + 0.00001f);
            CHECK(m::length(c.angular) <= m::kAlignmentAcceleration * dt + 0.00001f);
            const auto thrustError = m::mul(m::sub(m::rotate(follower.rotation, {0, 0, 1}),
                                                   m::rotate(guide.rotation, {0, 0, 1})),
                                            3);
            const auto disturbance = m::add(thrustError, {0.4f, -0.6f, 0.3f});
            follower.velocity = m::add(
                follower.velocity, m::add(c.linear, m::add(m::sub(guide.velocity, previousVelocity),
                                                           m::mul(disturbance, dt))));
            follower.angular = m::add(follower.angular, m::add(c.angular, {0, 0, 0.025f * dt}));
            follower.position = m::add(follower.position, m::mul(follower.velocity, dt));
            const float speed = m::length(follower.angular);
            if (speed > 0)
                follower.rotation =
                    m::product(rotation(follower.angular, speed * dt), follower.rotation);
        }
        CHECK(c.vertical);
        CHECK(m::length(c.error) < 1.0f);
        CHECK(c.angle < 0.02f);
    }
}

TEST_CASE("failed flight heights receive independent trim even during a hard turn") {
    // Values come from failed-01.log, not a zero-velocity fixture.
    m::Controller controller;
    m::Motion guide, follower;
    follower.position = {8, 0, 0};
    REQUIRE(controller.step(guide, follower, 1.0f / 30).apply);
    guide.velocity = {3.960f, 7.460f, 3.895f};
    follower.velocity = {3.488f, 7.206f, 4.633f};
    follower.position = {8.802f, 8.744f, -5.740f};
    auto c = controller.step(guide, follower, 1.0f / 30);
    CHECK(c.error.y == doctest::Approx(-8.744f));
    CHECK(c.linear.y == doctest::Approx(-m::kHeightAcceleration / 30));
    CHECK(c.linear.z > 0);
    CHECK(c.vertical);
    m::FormationHealth health;
    CHECK(std::string(health.step(c, true, 1.0f / 30)) == "drifting");
}

TEST_CASE("height bias is rejected without sacrificing the captured lateral lane") {
    for (const int hz : {30, 60, 120}) {
        const float dt = 1.0f / static_cast<float>(hz);
        m::Controller controller;
        m::Motion guide, follower;
        follower.position = {8, 0, 0};
        m::Command c;
        float worstHeight = 0;
        for (int i = 0; i < hz * 35; ++i) {
            c = controller.step(guide, follower, dt);
            REQUIRE(c.apply);
            // Apply persistent 4 m/s^2 upward and 2 m/s^2 forward disturbances from takeoff.
            follower.velocity = m::add(follower.velocity, m::add(c.linear, m::mul({0, 4, 2}, dt)));
            follower.position = m::add(follower.position, m::mul(follower.velocity, dt));
            worstHeight = std::max(worstHeight, std::fabs(c.error.y));
        }
        CHECK(worstHeight < 1);
        CHECK(m::length(c.error) < 0.03f);
    }
}

TEST_CASE("changing turn predicts tangential acceleration before positional drift") {
    m::Controller controller;
    m::Motion guide, follower;
    follower.position = {8, 0, 0};
    REQUIRE(controller.step(guide, follower, 1.0f / 30).apply);
    guide.angular = follower.angular = {0, 0.3f, 0};
    follower.velocity = {0, 0, -2.4f};
    auto c = controller.step(guide, follower, 1.0f / 30);
    CHECK(m::length(c.error) == 0);
    CHECK(c.speedError < 0.0001f);
    CHECK(c.yawAcceleration > 0);
    CHECK(c.turnAcceleration.z < -5);
    CHECK(c.linear.z < 0);
    guide.angular.y = follower.angular.y = 2;
    follower.velocity.z = -16;
    c = controller.step(guide, follower, 1.0f / 30);
    CHECK(m::length(c.turnAcceleration) <= m::kOrbitAcceleration + 0.0001f);
    CHECK(m::length(m::Vec{c.linear.x, 0, c.linear.z}) <= m::kTotalHorizontalAcceleration / 30 + 0.0001f);
    REQUIRE_FALSE(controller.step(guide, follower, 1).apply);
    guide.angular = follower.angular = {};
    follower.velocity = {};
    c = controller.step(guide, follower, 1.0f / 30);
    CHECK(c.yawAcceleration == 0);
}
TEST_CASE("bounded orbit assistance holds changing turns while controls remain engaged") {
    for (int hz : {30, 60, 120}) {
        const float dt = 1.0f / static_cast<float>(hz);
        m::Controller controller;
        m::Motion guide, follower;
        guide.velocity = follower.velocity = {0, 0, 15};
        follower.position = {8, 0, 0};
        float worstTurningError = 0;
        for (int i = 0; i < hz * 40; ++i) {
            const float t = static_cast<float>(i) * dt;
            const auto before = guide.velocity;
            guide.angular = {0, 1.3f * std::sin(0.4f * t), 0};
            guide.rotation = m::product(rotation({0, 1, 0}, guide.angular.y * dt), guide.rotation);
            guide.velocity = m::rotate(guide.rotation, {0, 0, 15});
            guide.position = m::add(guide.position, m::mul(guide.velocity, dt));
            follower.rotation = guide.rotation;
            follower.angular = guide.angular;
            const auto c = controller.step(guide, follower, dt);
            REQUIRE(c.apply);
            const auto relativeDrag = m::mul(m::sub(guide.velocity, follower.velocity), 0.3f);
            follower.velocity = m::add(follower.velocity, m::add(c.linear,
                m::add(m::sub(guide.velocity, before), m::mul(m::add(relativeDrag, {0.2f, 0.4f, -0.1f}), dt))));
            follower.position = m::add(follower.position, m::mul(follower.velocity, dt));
            if (i > hz * 3 && std::fabs(guide.angular.y) > 0.3f)
                worstTurningError = std::max(worstTurningError, m::length(c.error));
            CHECK(m::length(m::Vec{c.linear.x, 0, c.linear.z}) <= m::kTotalHorizontalAcceleration * dt + 0.0001f);
        }
        CHECK(worstTurningError < 1.5f);
    }
}

TEST_CASE("large separation and closing speed never permanently disable healthy recovery") {
    for (int hz : {30, 60, 120}) {
        const float dt = 1.0f / static_cast<float>(hz);
        m::Controller controller;
        m::Motion guide, follower;
        follower.position = {8, 0, 0};
        REQUIRE(controller.step(guide, follower, dt).apply);
        follower.position = {160, 30, 40};
        follower.velocity = {30, 15, 10};
        m::Command c;
        for (int i = 0; i < hz * 10; ++i) {
            c = controller.step(guide, follower, dt);
            REQUIRE(c.apply);
            follower.velocity = m::add(follower.velocity, c.linear);
            follower.position = m::add(follower.position, m::mul(follower.velocity, dt));
            CHECK(m::length(follower.velocity) < m::kMaximumMotionSpeed);
        }
        CHECK(m::length(c.error) < 0.5f);
        CHECK(m::length(follower.velocity) < 0.5f);
    }
    m::Controller fresh;
    m::Motion guide, remote;
    remote.position = {80, 10, 0};
    const auto first = fresh.step(guide, remote, 1.0f / 30);
    REQUIRE(first.apply);
    CHECK(first.error.x == doctest::Approx(-72));
    CHECK(first.error.y == doctest::Approx(-10));
    CHECK(first.linear.y < 0);
    guide.angular.y = 4;
    const auto fastTurn = fresh.step(guide, remote, 1.0f / 30);
    CHECK(fastTurn.apply);
    CHECK(m::length(m::Vec{fastTurn.linear.x, 0, fastTurn.linear.z}) <= m::kTotalHorizontalAcceleration / 30 + 0.0001f);
}

TEST_CASE("bad pair starts acquire a nearby side lane without collapsing multi-receiver layouts") {
    for (const float side : {-1.0f, 1.0f}) {
        m::Motion guide, follower;
        follower.position = {side * 80, 10, 1};
        m::Controller pair, fleet;
        auto compact = pair.step(guide, follower, 1.0f / 30, true);
        auto preserve = fleet.step(guide, follower, 1.0f / 30, false);
        REQUIRE(compact.apply); REQUIRE(preserve.apply);
        CHECK(compact.error.x == doctest::Approx(-side * 72));
        CHECK(compact.error.z == doctest::Approx(-1));
        CHECK(preserve.error.x == 0); CHECK(preserve.error.z == -1);
        follower.position = {side * 8, 0, 0};
        compact = pair.step(guide, follower, 1.0f / 30, true);
        CHECK(m::length(compact.error) < 0.0001f);
    }
    m::Controller stacked;
    m::Motion guide, follower;
    follower.position = {0, 10, 0};
    const auto c = stacked.step(guide, follower, 1.0f / 30);
    REQUIRE(c.apply);
    CHECK(c.error.x == 8); CHECK(c.error.y == -10);
}

TEST_CASE("strong commands fit the adapter even at the longest admitted timestep") {
    const float dt = 0.1f;
    const m::Vec dv{m::kTotalHorizontalAcceleration * dt, m::kHeightAcceleration * dt, 0};
    const m::Vec dw{0, m::kAlignmentAcceleration * dt, 0};
    CHECK(m::length(dw) <= m::kMaximumAngularDelta);
    CHECK(m::length(m::memberDelta(dv, dw, {0, 0, 10}, {})) < m::kMaximumMemberDelta);
    CHECK(m::kAlignmentRate > 0.85f * 4);
    CHECK(m::kAlignmentAcceleration > 2.5f * 7);
}

TEST_CASE("independent receiver attitude recovers during held turns despite opposing torque and half delivery") {
    for (int hz : {30, 60, 120}) {
        const float dt = 1.0f / static_cast<float>(hz);
        m::Controller controller;
        m::Motion guide, follower;
        guide.velocity = follower.velocity = {0, 0, 15};
        follower.position = {8, 5, 0};
        follower.rotation = rotation({0, 1, 0}, 2.8f);
        REQUIRE(controller.step(guide, follower, dt).apply);
        float worstLateAngle = 0, worstLateError = 0;
        for (int i = 0; i < hz * 20; ++i) {
            const float t = static_cast<float>(i) * dt;
            const auto beforeV = guide.velocity, beforeW = guide.angular;
            guide.angular = {0, 0.8f + 0.3f * std::sin(t * 0.8f), 0};
            guide.rotation = m::product(rotation({0, 1, 0}, guide.angular.y * dt), guide.rotation);
            guide.velocity = m::rotate(guide.rotation, {0, 0, 15});
            guide.position = m::add(guide.position, m::mul(guide.velocity, dt));
            const auto c = controller.step(guide, follower, dt);
            REQUIRE(c.apply);
            // Native body-local thrust follows the receiver's own attitude.
            const auto nativeDV = m::rotate(follower.rotation,
                m::rotate(m::transpose(guide.rotation), m::sub(guide.velocity, beforeV)));
            const auto drag = m::mul(m::sub(guide.velocity, follower.velocity), 0.2f);
            follower.velocity = m::add(follower.velocity, m::add(nativeDV,
                m::add(m::mul(c.linear, 0.5f), m::mul(m::add(drag, {0, 4, 0}), dt))));
            follower.angular = m::add(follower.angular, m::add(m::sub(guide.angular, beforeW),
                m::add(m::mul(c.angular, 0.5f), {0, 0.8f * dt, 0})));
            follower.position = m::add(follower.position, m::mul(follower.velocity, dt));
            const float speed = m::length(follower.angular);
            if (speed > 0) follower.rotation = m::product(rotation(follower.angular, speed * dt), follower.rotation);
            if (i > hz * 2) worstLateAngle = std::max(worstLateAngle, c.angle);
            if (i > hz * 6) worstLateError = std::max(worstLateError, m::length(c.error));
        }
        CHECK(worstLateAngle < 0.15f);
        CHECK(worstLateError < 1.0f);
    }
}

TEST_CASE("physics admission excludes paused maintenance sensor worlds and invalid timesteps") {
    CHECK(m::simulationPass(true, 5, true, 0, 1.0f / 30));
    CHECK(m::simulationPass(true, 15, false, 99, 1.0f / 60));
    CHECK_FALSE(m::simulationPass(false, 5, true, 0, 1.0f / 30));
    CHECK_FALSE(m::simulationPass(true, 4, true, 0, 1.0f / 30));
    CHECK_FALSE(m::simulationPass(true, 1, true, 0, 1.0f / 30));
    CHECK_FALSE(m::simulationPass(true, 5, true, 1, 1.0f / 30));
    CHECK_FALSE(m::simulationPass(true, 5, true, 2, 1.0f / 30));
    CHECK_FALSE(m::simulationPass(true, 5, true, 0, 0));
    CHECK_FALSE(m::simulationPass(true, 5, true, 0, 0.2f));
    CHECK_FALSE(m::simulationPass(true, 5, true, 0, std::numeric_limits<float>::quiet_NaN()));
}

TEST_CASE("request cache and actual motion differ and queue agreement is not delivery") {
    const m::Vec actual{4, 7, 12}, staleRequest{3, 4, 9}, trim{0, -0.25f, 0.1f};
    const auto noRequest = m::effectiveVelocity(actual, staleRequest, false);
    CHECK(noRequest.y == 7);
    const auto pending = m::effectiveVelocity(actual, staleRequest, true);
    CHECK(pending.y == 4);
    const auto expected = m::add(pending, trim);
    auto queue = expected;
    CHECK(m::length(m::sub(queue, expected)) == 0);
    CHECK_FALSE(m::velocityDelivered(expected, {}, actual, {}));
    auto consumed = queue;
    CHECK(m::velocityDelivered(expected, {}, consumed, {}));
    consumed = actual;
    CHECK_FALSE(m::velocityDelivered(expected, {}, consumed, {}));
    CHECK_FALSE(m::velocityDelivered(expected, {}, queue, {0, 0.1f, 0}));
}

TEST_CASE("HUD requires measured settling and holds brief delivery failures long enough to read") {
    m::FormationHealth health;
    m::Command c;
    for (int i = 0; i < 14; ++i)
        CHECK(std::string(health.step(c, true, 1.0f / 30)) == "correcting");
    for (int i = 0; i < 3; ++i) health.step(c, true, 1.0f / 30);
    CHECK(std::string(health.step(c, true, 1.0f / 30)) == "holding");
    c.error.y = -5;
    CHECK(std::string(health.step(c, true, 1.0f / 30)) == "drifting");
    CHECK(std::string(health.step(c, false, 1.0f / 30)) == "physics mismatch");
    for (int i = 0; i < 20; ++i)
        CHECK(std::string(health.step(c, true, 1.0f / 30)) == "physics mismatch");
    for (int i = 0; i < 20; ++i) health.step(c, true, 1.0f / 30);
    CHECK(std::string(health.step(c, true, 1.0f / 30)) == "drifting");
}
TEST_CASE(
    "native activity clock preserves elapsed time across sparse input calls and discards pause "
    "gaps") {
    m::ActivityClock clock{1000, 4};
    CHECK(clock.advance(1016, 4, 1000) == 0);
    CHECK(clock.advance(1033, 5, 1000) == doctest::Approx(0.033f));
    CHECK(clock.advance(1050, 6, 1000) == doctest::Approx(0.017f));
    CHECK(clock.advance(9999, 6, 1000) == 0);
    CHECK(clock.advance(10000, 7, 1000) == 0);
    CHECK(clock.advance(10016, 8, 1000) == doctest::Approx(0.016f));
}
