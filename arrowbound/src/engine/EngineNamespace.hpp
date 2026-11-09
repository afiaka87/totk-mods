// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// The engine adapters are compiled once per mod. Arrowbound's copy lives in `arrowbound::`; a host
// mod (Glideshot) compiles a second copy under its own namespace and log tag, so the two keep
// separate state (aim request slot, sound handles, player cache).
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
