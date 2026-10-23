// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "BombIdentity.hpp"
#include "BombVisualBackend.hpp"

TEST_CASE("native sphere keeps cube rendering and recognizes fused replacement identities") {
    using namespace remote_bomb;
    CHECK(nativeBomb(Shape::Sphere));
    CHECK_FALSE(nativeBomb(Shape::Cube));
    const auto sphere = identifyBomb("Obj_RemoteBombSpherePlus");
    const auto attached = identifyBomb("Obj_RemoteBombSpherePlus_Attachment");
    REQUIRE(sphere);
    REQUIRE(attached);
    CHECK(sphere->shape == attached->shape);
    CHECK(sphere->upgrade == attached->upgrade);
    CHECK_FALSE(identifyBomb("SpObj_TimerBomb_A_01"));
    CHECK_FALSE(identifyBomb("Obj_NativeSphereBlast"));
}
