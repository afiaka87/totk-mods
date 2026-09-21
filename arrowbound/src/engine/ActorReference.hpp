// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace arrowbound::engine {

// X8-returned ActorLinkBase reference; release it before the callback returns.
struct ActorReference {
    std::uintptr_t actor = 0;
    bool owns = false;

    ActorReference() = default;
    ActorReference(const ActorReference&) = delete;
    ActorReference& operator=(const ActorReference&) = delete;

    ~ActorReference() {
        if (!owns || !actor) return;
        std::atomic_ref<std::int32_t> count(*reinterpret_cast<std::int32_t*>(actor + 432));
        if (count.load(std::memory_order_relaxed) >= 1)
            count.fetch_sub(1, std::memory_order_relaxed);
    }
};

static_assert(sizeof(ActorReference) == 16);
static_assert(offsetof(ActorReference, actor) == 0);
static_assert(offsetof(ActorReference, owns) == 8);

}  // namespace arrowbound::engine
