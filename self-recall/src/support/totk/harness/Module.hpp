
#pragma once

#include <cstdint>

namespace wwpg {

using RaycastFn = std::uint64_t (*)(const void*, const void*, const void*, const void*,
                                    std::uint32_t, std::uint32_t);

struct Module {
    const char* name;
    const char* actors;
    const char* controls;
    const char* requirement;
    void (*init)(std::uintptr_t mainBase);
    void (*enter)();
    void (*tick)(void* npadDevice);
    bool (*requestExit)();
    const char* (*status)();
    void (*onRaycast)(RaycastFn original, const void* from, const void* to,
                      const void* object, const void* out, std::uint32_t mask,
                      std::uint32_t flag);
    const char* (*aim)();
};

}
