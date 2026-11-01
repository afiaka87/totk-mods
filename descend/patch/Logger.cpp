// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Runtime.hpp"
#include <program/loggers.hpp>
#include <cstdarg>
#include <cstdio>

namespace {
extern "C" void patch_debug_print(const char*, std::size_t);

consteval std::uint64_t packed(const char (&text)[9]) {
    std::uint64_t result = 0;
    for (int i = 0; i < 8; ++i) result |= std::uint64_t(static_cast<unsigned char>(text[i])) << (8 * i);
    return result;
}
}

void PatchLogger::Log(const char* format, ...) {
    char buffer[512];
    std::va_list arguments;
    va_start(arguments, format);
    const int result = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (result <= 0) return;
    const auto length = result < 512 ? result : 511;
    buffer[length] = '\0';
    patch_debug_print(buffer, static_cast<std::size_t>(length));
}

extern "C" void patch_status(std::uint32_t status, std::uint32_t detail) {
    // This runs before private data is published, including allocation failures. Volatile stores keep the format on the stack instead of in private rodata.
    volatile std::uint64_t format[6];
    format[0] = packed("[descend");
    format[1] = packed("] patch0");
    format[2] = packed("6 status");
    format[3] = packed("=%u code");
    format[4] = packed("=%08x\n\0\0");
    format[5] = 0;
    PatchLogger::Log(const_cast<const char*>(reinterpret_cast<volatile const char*>(format)), status, detail);
}
