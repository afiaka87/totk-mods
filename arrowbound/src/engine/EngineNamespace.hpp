// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Each mod compiles its own copy of the engine adapters, with its own namespace and log tag.
#pragma once

#include "Vec3.hpp"

#ifdef HOOKSHOT_ENGINE_NS
namespace HOOKSHOT_ENGINE_NS::pure {
using namespace arrowbound::pure;
}  // namespace HOOKSHOT_ENGINE_NS::pure
#else
#define HOOKSHOT_ENGINE_NS arrowbound
#define HOOKSHOT_ENGINE_TAG "[arrowbound]"
#endif
