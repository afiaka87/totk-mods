// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombReference.hpp"

namespace remote_bomb {
inline constexpr bool nativeBomb(Shape shape) {
#ifdef BOTW_ABILITIES_NATIVE_SPHERE
    if (shape == Shape::Sphere) return true;
#endif
#ifdef BOTW_ABILITIES_NATIVE_CUBE
    if (shape == Shape::Cube) return true;
#endif
    (void)shape;
    return false;
}
}
