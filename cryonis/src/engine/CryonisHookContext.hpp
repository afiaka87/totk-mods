// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisCamera.hpp"
#include "CryonisInput.hpp"
#include "CryonisTarget.hpp"
#include <atomic>
#include <cstdint>
namespace cryonis::engine {
// Hooks exchange scalar identities and snapshots; each presentation mailbox has one reader.
struct HookSignals {
    std::atomic<bool> recentKeyOwned{}, recentNativeOwned{}, running{}, aiming{};
    std::atomic<std::uintptr_t> keyPlayer{}, wheelOwner{}, cameraPlayer{}, cameraScene{};
    std::atomic<unsigned> nativeSelections{}, epoch{}, request{}, generation{1};
    std::atomic<std::uint64_t> wheel{}, clock{};
    std::atomic_flag cameraLock = ATOMIC_FLAG_INIT;
    Ray ray;
};
struct HookContext {
    HookSignals &signals;
    camera::Blend &camera;
    target::Mailbox &cast;
};
}
