// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

namespace remote_bomb::physics {
struct RequestBindings {
    void (*lock)(std::uintptr_t);
    void (*unlock)(std::uintptr_t);
    std::uintptr_t (*getOrAllocate)(std::uintptr_t);
};
enum class RefreshStatus { Queued, AlreadyQueued, UnsupportedBody, RequestUnavailable };
struct RefreshResult { RefreshStatus status; std::uintptr_t value; };
inline RefreshResult enqueueMotionRefresh(std::uintptr_t body, const RequestBindings &native) {
    const auto mutex = body + 120;
    native.lock(mutex);
    struct Unlock {
        const RequestBindings &native;
        std::uintptr_t mutex;
        ~Unlock() { native.unlock(mutex); }
    } unlock{native, mutex};
    std::uint64_t flags{};
    std::memcpy(&flags, reinterpret_cast<const void *>(body + 104), sizeof(flags));
    if ((flags & 0x100) || (flags & 0x1C) == 4)
        return {RefreshStatus::UnsupportedBody, flags};
    const auto request = native.getOrAllocate(body);
    if (!request)
        return {RefreshStatus::RequestUnavailable, 0};
    auto &bits = *reinterpret_cast<std::uint32_t *>(request + 212);
    if ((std::atomic_ref(bits).load(std::memory_order_relaxed) & 8) != 0)
        return {RefreshStatus::AlreadyQueued, request};
    unsigned preset{};
    std::memcpy(&preset, reinterpret_cast<const void *>(body + 380), sizeof(preset));
    std::memcpy(reinterpret_cast<void *>(request + 132), &preset, sizeof(preset));
    std::atomic_ref(bits).fetch_or(8, std::memory_order_relaxed);
    return {RefreshStatus::Queued, request};
}
}
