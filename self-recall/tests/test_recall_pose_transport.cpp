#include <array>
#include <cstring>
#include <limits>

#include "RecallPoseTransport.hpp"
#include "doctest.h"

using namespace self_recall::pure;

namespace {
RecordedBoneMatrix fixture() {
    const float columns[16] = {0, 2, 0, 0, -3, 0, 0, 0, 0, 0, 4, 0, 12, 34, 56, 0};
    RecordedBoneMatrix bone;
    std::memcpy(bone.words, columns, sizeof(columns));
    for (unsigned i = 3; i < 16; i += 4) bone.words[i] = 0x7fc00000u + i;
    return bone;
}

std::array<float, 3> transform(const float matrix[12], const std::array<float, 3>& point) {
    std::array<float, 3> result{};
    for (unsigned row = 0; row < 3; ++row) {
        result[row] = matrix[row * 4 + 3];
        for (unsigned col = 0; col < 3; ++col) result[row] += matrix[row * 4 + col] * point[col];
    }
    return result;
}
}

TEST_CASE("native padded bone columns produce the historical world wrist matrix") {
    const auto bone = fixture();
    RecordedModelPose model{{100, 200, 300}, 0, 1};
    float world[12]{};
    REQUIRE(boneToWorldMatrix(bone, model, world));
    const float expected[12] = {0, -3, 0, 112, 2, 0, 0, 234, 0, 0, 4, 356};
    for (unsigned i = 0; i < 12; ++i) CHECK(world[i] == expected[i]);
    const auto point = transform(world, {1, 2, 3});
    CHECK(point == std::array<float, 3>{106, 236, 368});
}

TEST_CASE("render-origin changes preserve historical world geometry") {
    const auto bone = fixture();
    for (std::uint32_t fromRelative : {0u, 1u}) {
        for (std::uint32_t toRelative : {0u, 1u}) {
            RecordedModelPose from{{100, 200, 300}, 0, fromRelative};
            RecordedModelPose to{{-1000, 500, 4096}, 0, toRelative};
            RecordedBoneMatrix rebased;
            REQUIRE(rebaseBoneForRender(bone, from, to, rebased));
            float before[12], after[12];
            REQUIRE(boneToWorldMatrix(bone, from, before));
            REQUIRE(boneToWorldMatrix(rebased, to, after));
            for (const auto& point : {std::array<float, 3>{0, 0, 0}, {1, 2, 3}, {-7, 2, -5}})
                CHECK(transform(before, point) == transform(after, point));
            for (unsigned word = 0; word < 16; ++word)
                if (word < 12 || word == 15) CHECK(rebased.words[word] == bone.words[word]);
        }
    }
}

TEST_CASE("unchanged render origins preserve translation bits and opaque padding") {
    auto bone = fixture();
    bone.words[12] = 0x80000000u;  // negative zero
    RecordedModelPose model{{1.0e20f, -1.0e20f, 1.0e20f}, 0, 1};
    RecordedBoneMatrix copy;
    REQUIRE(rebaseBoneForRender(bone, model, model, copy));
    CHECK(std::memcmp(&bone, &copy, sizeof(bone)) == 0);
}

TEST_CASE("nonfinite transforms fail without publishing partial output") {
    auto bone = fixture();
    RecordedModelPose model{};
    float world[12];
    for (auto& value : world) value = -77;
    bone.words[4] = 0x7f800000u;
    CHECK_FALSE(boneToWorldMatrix(bone, model, world));
    for (float value : world) CHECK(value == -77);
    bone = fixture();
    RecordedBoneMatrix destination = bone;
    model.renderOrigin[1] = std::numeric_limits<float>::infinity();
    model.originRelative = 1;
    CHECK_FALSE(rebaseBoneForRender(bone, {}, model, destination));
    CHECK(std::memcmp(&bone, &destination, sizeof(bone)) == 0);
    model.originRelative = 0;
    CHECK(rebaseBoneForRender(bone, {}, model, destination));
}
