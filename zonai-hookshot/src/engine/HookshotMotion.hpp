// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
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
