#include "RecallAppearanceHistory.hpp"
#include "RecallAppliedClimb.hpp"
#include "RecallControllerPose.hpp"
#include "doctest.h"
#include <thread>
using namespace self_recall::pure;

TEST_CASE("controller commit writes a complete interleaved matrix and only four velocity vectors") {
    std::array<float, 0x380 / sizeof(float)> actor;
    actor.fill(71.0f);
    std::array<float, 14> matrix;
    matrix.fill(89.0f);
    ControllerPoseOutput output{matrix.data() + 1, {actor.data() + 0x320 / 4,
        actor.data() + 0x32C / 4, actor.data() + 0x338 / 4, actor.data() + 0x344 / 4}};
    REQUIRE(output.playerCommit(reinterpret_cast<std::uintptr_t>(actor.data())));
    CHECK_FALSE(output.playerCommit(reinterpret_cast<std::uintptr_t>(actor.data()) + 4));
    CHECK_FALSE(output.playerCommit(0));
    Pose pose{};
    pose.rotation.values[1] = -1; pose.rotation.values[3] = 1; pose.rotation.values[8] = 1;
    pose.position = {1930.25f, 1362.5f, -1200.75f};
    const auto before = pose;
    REQUIRE(output.apply(pose));
    const std::array<float, 12> expected{0, -1, 0, 1930.25f, 1, 0, 0, 1362.5f, 0, 0, 1, -1200.75f};
    for (unsigned i = 0; i < expected.size(); ++i) CHECK(matrix[i + 1] == expected[i]);
    CHECK(matrix.front() == 89.0f);
    CHECK(matrix.back() == 89.0f);
    CHECK(std::memcmp(&pose, &before, sizeof(pose)) == 0);
    for (unsigned i = 0; i < actor.size(); ++i)
        CHECK(actor[i] == (i >= 0x320 / 4 && i < 0x350 / 4 ? 0.0f : 71.0f));
    for (unsigned i = 0; i < output.velocities.size(); ++i) {
        auto wrong = output;
        ++wrong.velocities[i];
        CHECK_FALSE(wrong.playerCommit(reinterpret_cast<std::uintptr_t>(actor.data())));
    }
}

TEST_CASE("invalid controller output or recorded pose leaves native results intact") {
    std::array<float, 12> matrix; matrix.fill(9.0f);
    std::array<float, 12> velocity; velocity.fill(7.0f);
    const auto originalMatrix = matrix, originalVelocity = velocity;
    ControllerPoseOutput output{matrix.data(), {velocity.data(), velocity.data() + 3,
        velocity.data() + 6, velocity.data() + 9}};
    Pose invalid{};
    invalid.position.y = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(output.apply(invalid));
    invalid.position.y = 0;
    invalid.rotation.values[8] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(output.apply(invalid));
    for (unsigned i = 0; i < output.velocities.size(); ++i) {
        auto absent = output;
        absent.velocities[i] = nullptr;
        CHECK_FALSE(absent.apply(Pose{}));
    }
    auto absent = output; absent.matrix = nullptr;
    CHECK_FALSE(absent.apply(Pose{}));
    CHECK(matrix == originalMatrix);
    CHECK(velocity == originalVelocity);
}

TEST_CASE("climb pose publication cannot be blocked by an actor reader or mix applied samples") {
    AppliedClimbMailbox mailbox;
    AppliedClimb copied;
    CHECK_FALSE(mailbox.snapshot(copied));
    std::atomic<bool> done{false};
    std::atomic<unsigned> mixed{0};
    std::thread reader([&] {
        while (!done.load()) {
            AppliedClimb value;
            if (mailbox.snapshot(value) &&
                (value.player != value.world || value.actorId != value.world ||
                 value.sample.pose.position.x != static_cast<float>(value.world))) ++mixed;
        }
    });
    for (unsigned i = 1; i <= 10000; ++i) {
        AppliedClimb value;
        value.player = value.actorId = value.world = i;
        value.sample.pose.position.x = static_cast<float>(i);
        mailbox.publish(value);
    }
    done.store(true);
    reader.join();
    CHECK(mixed.load() == 0);
    REQUIRE(mailbox.snapshot(copied));
    CHECK(copied.world == 10000);
    CHECK(copied.sample.pose.position.x == 10000);
}

TEST_CASE("equipment appearance survives source changes, frame replacement and owner retirement") {
    AppearanceBlobs<8, 8, 8> blobs;
    AppearanceFrames<2, 2> frames;
    blobs.initialize();
    std::array<std::byte, 13> material{};
    material[0] = std::byte{1}; // true form
    material[12] = std::byte{255}; // visibility/effect property, in another block
    const auto glowing = blobs.create(material);
    REQUIRE(glowing);
    REQUIRE(frames.bind(blobs, {1, 1, 0}, std::array<unsigned, 2>{0, glowing}));
    CHECK(blobs.equal(glowing, material));
    material[0] = material[12] = std::byte{0};
    const auto removed = blobs.create(material);
    REQUIRE(removed);
    REQUIRE(frames.bind(blobs, {2, 1, 1}, std::array<unsigned, 2>{0, removed}));
    blobs.release(glowing);
    blobs.release(removed);
    std::array<std::byte, 13> recalled{};
    REQUIRE(blobs.copy(frames.token({1, 1, 0}, 1), recalled));
    CHECK(recalled[0] == std::byte{1});
    CHECK(recalled[12] == std::byte{255});
    REQUIRE(blobs.copy(frames.token({2, 1, 1}, 1), recalled));
    CHECK(recalled == material);
    REQUIRE(frames.bind(blobs, {3, 1, 0}, std::array<unsigned, 2>{0, glowing}));
    CHECK(frames.token({1, 1, 0}, 1) == 0);
    CHECK(frames.token({3, 2, 0}, 1) == 0);
    CHECK(frames.token({3, 1, 0}, 1) == glowing);
    frames.clear(blobs);
    CHECK(blobs.availableBytes() == 64);
    CHECK(blobs.size(glowing) == 0);
    CHECK(blobs.size(removed) == 0);
}

TEST_CASE("appearance exhaustion and invalid frame publication leave retained history intact") {
    AppearanceBlobs<3, 3, 8> blobs;
    AppearanceFrames<2, 2> frames;
    blobs.initialize();
    const std::array<std::byte, 17> material{std::byte{23}};
    const auto token = blobs.create(material);
    REQUIRE(token);
    REQUIRE(frames.bind(blobs, {1, 1, 0}, std::array<unsigned, 2>{token, token}));
    blobs.release(token);
    CHECK(blobs.create(material) == 0);
    CHECK_FALSE(frames.bind(blobs, {2, 1, 0}, std::array<unsigned, 2>{token, 3}));
    CHECK(frames.token({1, 1, 0}, 0) == token);
    CHECK(blobs.equal(token, material));
    frames.clear(blobs);
    CHECK(blobs.availableBytes() == 24);
    CHECK(blobs.create(material) != 0);
}

TEST_CASE("applied climb ownership refuses another actor, scene, nonclimb and invalid pose") {
    AppliedClimb applied;
    applied.player = 0x1234; applied.actorId = 42; applied.world = 7;
    applied.sample.flags = SampleAdmissible | SampleClimb;
    REQUIRE(matchesAppliedClimb(applied, 0x1234, 42, 7));
    CHECK_FALSE(matchesAppliedClimb(applied, 0x1235, 42, 7));
    CHECK_FALSE(matchesAppliedClimb(applied, 0x1234, 43, 7));
    CHECK_FALSE(matchesAppliedClimb(applied, 0x1234, 42, 8));
    applied.sample.flags = SampleAdmissible;
    CHECK_FALSE(matchesAppliedClimb(applied, 0x1234, 42, 7));
    applied.sample.flags = SampleClimb;
    CHECK_FALSE(matchesAppliedClimb(applied, 0x1234, 42, 7));
    applied.sample.flags = SampleAdmissible | SampleClimb;
    applied.sample.pose.position.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(matchesAppliedClimb(applied, 0x1234, 42, 7));
}

TEST_CASE("applied route ownership survives leaving the climb state without admitting foreign actors") {
    AppliedClimb applied;
    applied.player = 0x1234; applied.actorId = 42; applied.world = 7;
    applied.sample.flags = SampleAdmissible;
    CHECK(matchesAppliedPose(applied, 0x1234, 42, 7));
    CHECK_FALSE(matchesAppliedClimb(applied, 0x1234, 42, 7));
    CHECK_FALSE(matchesAppliedPose(applied, 0x1234, 43, 7));
    CHECK_FALSE(matchesAppliedPose(applied, 0x1234, 42, 8));
    applied.sample.flags = 0;
    CHECK_FALSE(matchesAppliedPose(applied, 0x1234, 42, 7));
}
