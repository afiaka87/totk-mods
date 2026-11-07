// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <cstdarg>
#include <cstddef>

namespace botw_abilities::support {

// Formats like vsnprintf and returns the length the full text would have, so callers that clamp a
// result keep working. Covers d i u o x X c s p f F e E g G %, the usual flags, width, precision and
// the hh h l ll z j t sizes. Long double, wide text and %n are not supported, and an unknown
// conversion is copied through as written.
int formatText(char* out, std::size_t size, const char* format, std::va_list args);

}
