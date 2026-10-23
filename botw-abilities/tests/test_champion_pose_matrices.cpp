// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "GalePoseMatrices.hpp"
#include "GaleModels.hpp"
#include <limits>

using namespace botw_abilities::champions;
namespace {
PoseMatrix translated(float x, float y, float z) {
    PoseMatrix result;
    result.at(0,3) = x; result.at(1,3) = y; result.at(2,3) = z;
    return result;
}
void equal(const PoseMatrix& actual, const PoseMatrix& expected) {
    for (unsigned i = 0; i < 16; ++i) CHECK(actual.values[i] == doctest::Approx(expected.values[i]).epsilon(1e-5));
}
}

TEST_CASE("Gale matrices use native column major storage and parent first composition") {
    const auto half = std::sqrt(.5f);
    const std::array<LocalPose, 3> locals{{{{3,0,0}, {0,half,0,half}}, {{0,0,2}}, {{0,1,0}}}};
    const int parents[]{-1,0,1};
    std::array<PoseMatrix, 3> world;
    REQUIRE(buildPoseHierarchy(locals, [&](unsigned i) { return parents[i]; }, world));
    CHECK(world[0].values[12] == 3);
    CHECK(world[1].at(0,3) == doctest::Approx(5));
    CHECK(world[1].at(2,3) == doctest::Approx(0).epsilon(1e-6));
    CHECK(world[2].at(1,3) == 1);
    const auto oldRoot = world[0];
    const auto child = world[2];
    REQUIRE(anchorPose(world, translated(100,20,-4)));
    equal(world[0], translated(100,20,-4));
    CHECK(world[2].at(0,3) == 100);
    CHECK(world[2].at(1,3) == 21);
    CHECK(world[2].at(2,3) == doctest::Approx(-2));
    PoseMatrix returned;
    REQUIRE(followPoseParent(world[2], world[0], oldRoot, returned));
    equal(returned, child);
}

TEST_CASE("Gale affine inverse preserves scaled gear attachments and aliasing") {
    auto oldParent = translated(1,2,3);
    oldParent.at(0,0) = 2; oldParent.at(1,1) = 3; oldParent.at(2,2) = 4;
    PoseMatrix inverse, composed;
    REQUIRE(inversePose(oldParent, inverse));
    REQUIRE(composePose(oldParent, inverse, composed));
    equal(composed, PoseMatrix{});
    auto gear = translated(5,8,15);
    const auto half = std::sqrt(.5f);
    PoseMatrix nextParent;
    REQUIRE(localPoseMatrix({{10,0,0}, {0,half,0,half}}, nextParent));
    REQUIRE(followPoseParent(gear, oldParent, nextParent, gear));
    CHECK(gear.at(0,3) == doctest::Approx(13));
    CHECK(gear.at(1,3) == doctest::Approx(2));
    CHECK(gear.at(2,3) == doctest::Approx(-2));
}

TEST_CASE("Gale clothing retains bind offsets and extra cloth follows the nearest mapped parent") {
    const std::array<PoseMatrix, 2> oldBody{PoseMatrix{}, translated(0,1,0)};
    const std::array<PoseMatrix, 2> newBody{PoseMatrix{}, translated(0,.4f,0)};
    const std::array<PoseMatrix, 3> cloth{PoseMatrix{}, translated(.05f,1,0), translated(.1f,.8f,0)};
    const int mapping[]{0,1,-1}, parents[]{-1,0,1};
    std::array<PoseMatrix, 3> result;
    REQUIRE(retargetPose(cloth, [&](unsigned i) { return mapping[i]; }, [&](unsigned i) { return parents[i]; },
                         oldBody, newBody, result));
    CHECK(result[1].at(0,3) == doctest::Approx(.05));
    CHECK(result[1].at(1,3) == doctest::Approx(.4));
    CHECK(result[2].at(0,3) == doctest::Approx(.1));
    CHECK(result[2].at(1,3) == doctest::Approx(.2));
}

TEST_CASE("Gale pose rebases render origins and refuses invalid matrices or hierarchies") {
    auto matrix = translated(1,2,3);
    REQUIRE(rebasePoseOrigin(matrix, {10000,0,-10000}, {9990,0,-9990}));
    equal(matrix, translated(11,2,-7));
    const auto good = matrix;
    CHECK_FALSE(rebasePoseOrigin(matrix, {std::numeric_limits<float>::infinity(),0,0}, {}));
    equal(matrix, good);
    auto singular = PoseMatrix{};
    singular.at(0,0) = 0;
    CHECK_FALSE(inversePose(singular, matrix));
    equal(matrix, good);
    singular = PoseMatrix{}; singular.at(3,0) = 1;
    CHECK_FALSE(composePose(singular, good, matrix));
    const LocalPose locals[2]{};
    std::array<PoseMatrix, 2> result;
    CHECK_FALSE(buildPoseHierarchy(locals, [](unsigned) { return 0; }, result));
    CHECK_FALSE(buildPoseHierarchy(locals, [](unsigned i) { return i == 0 ? -1 : 1; }, result));
    CHECK_FALSE(retargetPose(std::span<const PoseMatrix>(result), [](unsigned) { return -1; },
                             [](unsigned) { return -1; }, result, result, result));
}

TEST_CASE("Gale native reads refuse addresses outside the Switch user range and overflowing spans") {
    const auto pointer = [](std::uintptr_t address) { return reinterpret_cast<const void*>(address); };
    constexpr auto limit = std::uintptr_t{1} << 39;
    CHECK(pose::models::admit(pointer(0x1000), 0, 1));
    CHECK(pose::models::admit(pointer(limit - 1), 0, 1));
    CHECK_FALSE(pose::models::admit(pointer(0), 0x1000, 1));
    CHECK_FALSE(pose::models::admit(pointer(0xFFF), 0, 1));
    CHECK_FALSE(pose::models::admit(pointer(limit), 0, 1));
    CHECK_FALSE(pose::models::admit(pointer(limit - 1), 0, 2));
    CHECK_FALSE(pose::models::admit(pointer(0x1000), limit - 0x1000, 1));
    CHECK_FALSE(pose::models::admit(pointer(0x1000), std::numeric_limits<std::size_t>::max(), 1));
    CHECK_FALSE(pose::models::admit(pointer(0x1000), 0, std::numeric_limits<std::size_t>::max()));
}
