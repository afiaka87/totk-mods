// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87

// One log prefix for every service.
#pragma once

#include <lib.hpp>

#define ZHLOG(...) Logging.Log("[zonai-hookshot] " __VA_ARGS__)
