// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
namespace stasis::surface {
void install();
bool scheduled();
bool masks(void *&depth, void *&hoverDepth);
} // namespace stasis::surface
