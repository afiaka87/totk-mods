// Compile the actual coordinator against an inert hook/logger boundary on the host.
#pragma once
#ifdef _MSC_VER
#include <intrin.h>
#define __builtin_return_address(level) _ReturnAddress()
#endif
#include <cstddef>
#include <cstdint>
struct TestLogger { template<class... Args> void Log(const char*, Args...) {} };
inline TestLogger Logging;
struct TestSortHook {
    inline static std::uintptr_t (*Orig)(std::uintptr_t, std::uintptr_t, int, unsigned) = nullptr;
    static void InstallAtOffset(std::uintptr_t) {}
};
#ifndef HOOK_DEFINE_TRAMPOLINE
#define HOOK_DEFINE_TRAMPOLINE(Name) struct Name : TestSortHook
#endif

struct TestInlineHook { static void InstallAtOffset(std::uintptr_t) {} };
namespace exl::hook { struct InlineCtx { std::uintptr_t X[31]{}; }; }
#ifndef HOOK_DEFINE_INLINE
#define HOOK_DEFINE_INLINE(Name) struct Name : TestInlineHook
#endif
