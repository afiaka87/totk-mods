// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define _CRT_SECURE_NO_WARNINGS
#include <doctest.h>
#include "GalePose.hpp"
#include "GalePoseMatrices.hpp"
#include "GalePoseTime.hpp"
#include <array>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <vector>

namespace {
using namespace botw_abilities::champions;

struct Fixture {
    std::array<std::byte, 324> bytes{};
    template<class T> void put(std::size_t offset, T value) { std::memcpy(bytes.data() + offset, &value, sizeof(value)); }
    void seal(std::size_t size = 324) {
        std::uint32_t crc = ~0u;
        for (std::size_t i = 20; i < size; ++i) {
            crc ^= std::to_integer<unsigned char>(bytes[i]);
            for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
        }
        put(16, ~crc);
    }
    Fixture() {
        std::memcpy(bytes.data(), "CBGP", 4);
        put<std::uint16_t>(4, 2); put<std::uint16_t>(6, 1); put<std::uint16_t>(8, 3);
        put<std::uint16_t>(10, 28); put(12, .5f);
        std::memcpy(bytes.data() + 20, "Root", 4);
        put<std::int16_t>(52, -1); put<std::uint16_t>(54, 3);
        const char* names[]{"Nml_Wait_To_Squat_Wait", "Squat_Wait_FootL", "Squat_Wait_FootR"};
        for (unsigned i = 0; i < 3; ++i) {
            const auto offset = 56 + i * 40;
            std::memcpy(bytes.data() + offset, names[i], std::strlen(names[i]));
            put<std::uint16_t>(offset + 32, 2); put<std::uint16_t>(offset + 34, i != 0);
            put<std::uint32_t>(offset + 36, i * 2);
            put(204 + i * 40, std::array<float, 3>{}); put(216 + i * 40, std::array<std::int16_t, 4>{0, 0, 0, 32767});
            put(224 + i * 40, std::array<float, 3>{10, 0, 0}); put(236 + i * 40, std::array<std::int16_t, 4>{0, 0, 0, -32767});
        }
        put(176, LocalPose{});
        seal();
    }
};
}

TEST_CASE("Gale pose interpolates motion and equivalent quaternion signs") {
    Fixture f;
    const auto asset = GalePose::parse(f.bytes);
    REQUIRE(asset);
    CHECK(asset->boneCount() == 1);
    CHECK(asset->boneName(0) == "Root");
    CHECK(asset->parent(0) == -1);
    CHECK(asset->duration(0) == .5f);
    LocalPose pose;
    REQUIRE(asset->sample(0, .25f, {&pose, 1}));
    CHECK(pose.position[0] == 5);
    CHECK(std::abs(pose.rotation[3]) == 1);
    REQUIRE(asset->sample(0, 100, {&pose, 1}));
    CHECK(pose.position[0] == 10);
    REQUIRE(asset->sample(1, .5f, {&pose, 1}));
    CHECK(pose.position[0] == 0);
    CHECK_FALSE(asset->sample(3, 0, {&pose, 1}));
    CHECK_FALSE(asset->sample(0, 0, {}));
    CHECK_FALSE(asset->sample(0, std::numeric_limits<float>::infinity(), {&pose, 1}));
    CHECK(asset->sample(1, std::numeric_limits<float>::max(), {&pose, 1}));
}

TEST_CASE("Gale pose refuses truncated or corrupted assets") {
    Fixture f;
    for (std::size_t size = 0; size < f.bytes.size(); ++size)
        CHECK_FALSE(GalePose::parse(std::span(f.bytes).first(size)));
    f.bytes.back() ^= std::byte{1};
    CHECK_FALSE(GalePose::parse(f.bytes));
}

TEST_CASE("Gale pose uses native frame deltas once per simulation tick and resets per channel") {
    PoseTime clock;
    clock = advancePoseTime(clock, 2, 100, .5f);
    CHECK(clock.frames == .5f);
    CHECK(advancePoseTime(clock, 2, 100, .5f).frames == .5f);
    clock = advancePoseTime(clock, 2, 101, .25f);
    CHECK(clock.frames == .75f);
    CHECK(advancePoseTime(clock, 2, 102, 0).frames == .75f);
    CHECK(advancePoseTime(clock, 2, 102, -1).tick == 101);
    CHECK(advancePoseTime(clock, 2, 102, std::numeric_limits<float>::infinity()).tick == 101);
    clock = advancePoseTime(clock, 3, 101, .5f);
    CHECK(clock.frames == .5f);
    CHECK(clock.channel == 3);
    CHECK(advancePoseTime(clock, 0, 103, 1).channel == 0);
}

TEST_CASE("Gale channel pose joins its intro to either foot loop with the native six frame morph") {
    Fixture fixture;
    const auto pose = GalePose::parse(fixture.bytes);
    REQUIRE(pose);
    LocalPose out, morph;
    const auto sample = [&](float frames, unsigned loop) { return pose->sampleChannel(frames, loop, {&out,1}, {&morph,1}); };
    REQUIRE(sample(.25f, 1));
    CHECK(out.position[0] == 5);
    REQUIRE(sample(.5f, 1));
    CHECK(out.position[0] == 10);
    REQUIRE(sample(.75f, 2));
    CHECK(out.position[0] == doctest::Approx(10 + .25f / 6 * (5 - 10)));
    CHECK(std::abs(out.rotation[3]) == 1);
    REQUIRE(sample(6.75f, 2));
    CHECK(out.position[0] == 5);
    CHECK_FALSE(sample(-1, 1));
    CHECK_FALSE(sample(0, 0));
    CHECK_FALSE(sample(0, 3));
    CHECK_FALSE(pose->sampleChannel(.75f, 1, {&out,1}, {&out,1}));
}

TEST_CASE("Gale pose validates ranges hierarchy names and finite transforms") {
    const auto reject = [](auto mutate) {
        Fixture f;
        mutate(f); f.seal();
        CHECK_FALSE(GalePose::parse(f.bytes));
    };
    reject([](Fixture& f) { f.put<std::int16_t>(52, 0); });
    reject([](Fixture& f) { f.put<std::uint32_t>(92, 1); });
    reject([](Fixture& f) { f.put<std::uint16_t>(88, 0); });
    reject([](Fixture& f) { f.put<std::uint16_t>(90, 1); });
    reject([](Fixture& f) { f.put(12, std::numeric_limits<float>::denorm_min()); });
    reject([](Fixture& f) { f.put(176, std::numeric_limits<float>::quiet_NaN()); });
    reject([](Fixture& f) { f.put(200, 0.0f); });
    reject([](Fixture& f) { f.put(204, std::numeric_limits<float>::quiet_NaN()); });
    reject([](Fixture& f) { f.put(216, std::array<std::int16_t, 4>{}); });
    reject([](Fixture& f) { f.put<std::uint16_t>(54, 4); });
    reject([](Fixture& f) { f.put<std::uint16_t>(4, 1); });
    reject([](Fixture& f) { std::memset(f.bytes.data() + 20, 'x', 32); });
}

TEST_CASE("Gale pose continues the intro with the foot loop that starts where it ends") {
    Fixture f;
    const auto same = GalePose::parse(f.bytes);
    REQUIRE(same);
    CHECK(same->continuation() == 1);
    f.put(256, std::array<std::int16_t, 4>{32767, 0, 0, 0});
    f.seal();
    const auto changed = GalePose::parse(f.bytes);
    REQUIRE(changed);
    CHECK(changed->continuation() == 2);
}

TEST_CASE("Gale pose keeps unchanging channels from the rest pose") {
    Fixture f;
    f.put<std::uint16_t>(54, 2);
    LocalPose rest;
    rest.position[1] = 3;
    f.put(176, rest);
    std::array<std::byte, 324> compact{};
    std::memcpy(compact.data(), f.bytes.data(), 204);
    for (unsigned sample = 0; sample < 6; ++sample)
        std::memcpy(compact.data() + 204 + sample * 8, f.bytes.data() + 216 + sample * 20, 8);
    f.bytes = compact;
    f.seal(204 + 6 * 8);
    const auto asset = GalePose::parse(std::span(f.bytes).first(204 + 6 * 8));
    REQUIRE(asset);
    LocalPose pose;
    REQUIRE(asset->sample(0, .25f, {&pose, 1}));
    CHECK(pose.position[0] == 0);
    CHECK(pose.position[1] == 3);
    CHECK(std::abs(pose.rotation[3]) == 1);
}

TEST_CASE("Gale pose generated asset can be read and sampled" * doctest::skip()) {
    const char* path = std::getenv("CHAMPION_GALE_POSE");
    REQUIRE(path != nullptr);
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    const std::vector<char> chars((std::istreambuf_iterator<char>(file)), {});
    const auto asset = GalePose::parse(std::as_bytes(std::span(chars)));
    REQUIRE(asset);
    CHECK(asset->boneCount() == 121);
    CHECK(asset->continuation() == 1);
    std::array<LocalPose, 128> pose;
    for (unsigned clip = 0; clip < 3; ++clip)
        for (float frame = 0; frame < asset->duration(clip); frame += .125f)
            REQUIRE(asset->sample(clip, frame, pose));
    REQUIRE(asset->sample(0, asset->duration(0), pose));
    std::array<PoseMatrix, 128> world;
    const auto parents = [&](unsigned bone) { return asset->parent(bone); };
    REQUIRE(buildPoseHierarchy(std::span(pose).first(asset->boneCount()), parents, world));
    const auto find = [&](std::string_view name) {
        for (unsigned bone = 0; bone < asset->boneCount(); ++bone)
            if (asset->boneName(bone) == name) return bone;
        return unsigned(world.size());
    };
    const auto pelvis = find("Skl_Root"), wrist = find("Wrist_R");
    REQUIRE(pelvis < asset->boneCount());
    REQUIRE(wrist < asset->boneCount());
    CHECK(world[pelvis].at(1,3) == doctest::Approx(.4386).epsilon(.001));
    CHECK(world[wrist].at(1,3) == doctest::Approx(.1466).epsilon(.001));
    PoseMatrix anchor;
    anchor.at(0,0) = -1; anchor.at(2,2) = -1;
    anchor.at(0,3) = 120; anchor.at(1,3) = 30; anchor.at(2,3) = -200;
    const auto original = world[wrist];
    REQUIRE(anchorPose(std::span(world).first(asset->boneCount()), anchor));
    CHECK(world[wrist].at(0,3) == doctest::Approx(120 - original.at(0,3)));
    CHECK(world[wrist].at(1,3) == doctest::Approx(30 + original.at(1,3)));
    CHECK(world[wrist].at(2,3) == doctest::Approx(-200 - original.at(2,3)));
    std::array<LocalPose,128> morph;
    for (unsigned loop : {1u,2u})
        for (float frame : {0.0f,11.9f,12.0f,12.5f,15.0f,17.99f,18.0f,59.9f,60.0f,2000.0f}) {
            REQUIRE(asset->sampleChannel(frame, loop, pose, morph));
            REQUIRE(buildPoseHierarchy(std::span(pose).first(asset->boneCount()), parents, world));
        }
}

TEST_CASE("Gale crouch fills the whole charge and continues from the intro's end") {
    using botw_abilities::champions::channelPoseFrames;
    CHECK(channelPoseFrames(0,12,30) == 0);
    CHECK(channelPoseFrames(15,12,30) == doctest::Approx(6));
    CHECK(channelPoseFrames(30,12,30) == doctest::Approx(12));
    CHECK(channelPoseFrames(40,12,30) == doctest::Approx(22));
    CHECK(channelPoseFrames(15,0,30) == 15);
    CHECK(channelPoseFrames(15,40,30) == 15);
}
