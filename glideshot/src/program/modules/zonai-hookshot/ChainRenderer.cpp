// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include <lib.hpp>
#include <nn/os.h>

#include <cstring>

#include "ChainRenderer.hpp"

namespace zonai_hookshot::render {
namespace {
nn::os::MutexType g_lock{};
Snapshot g_snapshot{};
bool g_configured = false;
bool g_published = false;

}  // namespace

void configure(uintptr_t mainBase) {
    (void)mainBase;
    if (g_configured) return;
    nn::os::InitializeMutex(&g_lock, true, 0);
    g_configured = true;
    Logging.Log("[zonai-hookshot] chain snapshot mailbox configured");
}

void publish(const Snapshot& snapshot) {
    if (!g_configured || !nn::os::TryLockMutex(&g_lock)) return;
    g_snapshot = snapshot;
    g_published = true;
    nn::os::UnlockMutex(&g_lock);
}

bool takeSnapshot(Snapshot& out) {
    if (!g_configured || !g_published) return false;
    if (!nn::os::TryLockMutex(&g_lock)) return false;
    out = g_snapshot;
    nn::os::UnlockMutex(&g_lock);
    return true;
}

}  // namespace zonai_hookshot::render
