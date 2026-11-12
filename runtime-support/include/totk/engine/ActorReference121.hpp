// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>
#include <type_traits>
#if defined(TOTK_PORT) && TOTK_PORT
#include "totk/port/Port.hpp"
#endif

namespace totk::engine {
struct NativeActorReferenceResult {
    const void* actor = nullptr;
    std::uint8_t counted = 0;
    std::uint8_t padding[7]{};
    ~NativeActorReferenceResult() {}
};
static_assert(sizeof(NativeActorReferenceResult) == 16);
static_assert(!std::is_trivially_destructible_v<NativeActorReferenceResult>);

// With TOTK_PORT both routines translate to the running game (same arguments and result on 1.4.3).
#if defined(TOTK_PORT) && TOTK_PORT
inline namespace ported {
#endif
struct ActorReference121Functions {
    NativeActorReferenceResult (*resolve)(const void*);
    void (*clear)(NativeActorReferenceResult*, const void*);
    static ActorReference121Functions fromMainBase(std::uintptr_t base) {
#if defined(TOTK_PORT) && TOTK_PORT
        using namespace port::literals;
        return {reinterpret_cast<decltype(resolve)>(base + 0x753530_v121),
                reinterpret_cast<decltype(clear)>(base + 0x86A288_v121)};
#else
        return {reinterpret_cast<decltype(resolve)>(base + 0x753530),
                reinterpret_cast<decltype(clear)>(base + 0x86A288)};
#endif
    }
};

class ActorReference121 {
public:
    ActorReference121(std::uintptr_t base, const void* link)
        : ActorReference121(ActorReference121Functions::fromMainBase(base), link) {}
    ActorReference121(ActorReference121Functions functions, const void* link)
        : functions_(functions), reference_(functions.resolve(link)) {}
    ~ActorReference121() { if (reference_.actor) functions_.clear(&reference_, nullptr); }
    ActorReference121(const ActorReference121&) = delete;
    ActorReference121& operator=(const ActorReference121&) = delete;
    const void* get() const { return reference_.actor; }
private:
    ActorReference121Functions functions_;
    NativeActorReferenceResult reference_;
};
#if defined(TOTK_PORT) && TOTK_PORT
}  // inline namespace ported
#endif
} // namespace totk::engine
