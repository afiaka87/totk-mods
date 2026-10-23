// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "BombIdentity.hpp"
#include "BombVisualBackend.hpp"

TEST_CASE("both native bombs recognize their fused replacements without adopting blast actors") {
    using namespace remote_bomb;
    for (const auto shape : {Shape::Sphere, Shape::Cube})
        CHECK(nativeBomb(shape));
    for (const auto upgrade : {Upgrade::Base, Upgrade::Plus}) {
        const auto attached = identifyBomb(upgrade == Upgrade::Plus
            ? "Obj_RemoteBombCubePlus_Attachment" : "Obj_RemoteBombCube_Attachment");
        REQUIRE(attached);
        CHECK(attached->shape == Shape::Cube);
        CHECK(attached->upgrade == upgrade);
    }
    CHECK_FALSE(identifyBomb("SpObj_TimerBomb_A_01"));
    CHECK_FALSE(identifyBomb("Obj_NativeSphereBlast"));
}
