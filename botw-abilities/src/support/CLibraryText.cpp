// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
// Replaces the C library's snprintf family for the module. The library version links about 40 KB of
// code and keeps its number conversion on the module heap, which is the only sizeable use of that heap.
#include <cstdarg>
#include <cstddef>

#include "TextFormat.hpp"

extern "C" int vsnprintf(char* out, std::size_t size, const char* format, std::va_list args) {
    return botw_abilities::support::formatText(out, size, format, args);
}

extern "C" int snprintf(char* out, std::size_t size, const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    const int length = botw_abilities::support::formatText(out, size, format, args);
    va_end(args);
    return length;
}
