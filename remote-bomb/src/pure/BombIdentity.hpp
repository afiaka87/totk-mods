// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombReference.hpp"
#include <optional>
#include <string_view>

namespace remote_bomb {
struct BombIdentity { Shape shape; Upgrade upgrade; };
inline constexpr const char *kActorNames[2][2] = {
    {"Obj_RemoteBombSphere", "Obj_RemoteBombSpherePlus"},
    {"Obj_RemoteBombCube", "Obj_RemoteBombCubePlus"},
};
inline constexpr std::optional<BombIdentity> identifyBomb(std::string_view name) {
    for (unsigned form = 0; form < 2; ++form)
        for (unsigned upgrade = 0; upgrade < 2; ++upgrade)
            if (name == kActorNames[form][upgrade])
                return BombIdentity{form ? Shape::Cube : Shape::Sphere,
                                    upgrade ? Upgrade::Plus : Upgrade::Base};
    return std::nullopt;
}
}
