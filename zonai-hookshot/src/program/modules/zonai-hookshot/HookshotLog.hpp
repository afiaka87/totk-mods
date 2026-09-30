// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// One log prefix for every service.
#pragma once

#include <lib.hpp>

#define ZHLOG(...) Logging.Log("[zonai-hookshot] " __VA_ARGS__)
