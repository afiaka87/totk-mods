// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace zonai_hookshot {
struct HookshotRuntime;
namespace motion {
void install(std::uintptr_t base);
void publish(const HookshotRuntime& runtime);
void reset();
}
}
