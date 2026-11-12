// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87

// Each mod compiles its own engine-adapter copy in its own namespace, so aim slot, sounds and caches stay separate.
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
