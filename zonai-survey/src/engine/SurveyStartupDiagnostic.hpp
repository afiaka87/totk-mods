// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace sead { class Heap; }

namespace zonai_survey::engine::startup_diagnostic {

void begin();
void installFailureHooks(std::uintptr_t mainBase);
void mark(const char* stage, std::uint64_t value0 = 0, std::uint64_t value1 = 0);
void captureCreateArgs(std::uintptr_t nativeArguments, std::uintptr_t debugArguments);
void lateGraphicsInit();

} // namespace zonai_survey::engine::startup_diagnostic
