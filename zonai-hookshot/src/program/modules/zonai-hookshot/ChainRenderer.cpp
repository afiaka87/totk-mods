// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87

#include <lib.hpp>
#include <nn/os.h>

#include <cstring>

#include "ChainRenderer.hpp"
#include "PlayerPresentation.hpp"

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
    zonai_hookshot::pure::Vec3 hand{};
    if(out.visible&&playerPresentation::usesHandOrigin()) {
        // Diagnostic: log why and how often the chain hides for want of the hand position, and when it returns.
        static unsigned hiddenReason = 0, hiddenDraws = 0;
        const unsigned reason = playerPresentation::handOriginState(hand);
        if(!reason) {
            if(hiddenDraws) Logging.Log("[zonai-hookshot] CHAIN_SHOWN after_hidden_draws=%u last_reason=%u", hiddenDraws, hiddenReason);
            hiddenDraws = 0;
            out.nearPoint[0]=hand.x;out.nearPoint[1]=hand.y;out.nearPoint[2]=hand.z;
        } else {
            if(!hiddenDraws || reason != hiddenReason)
                Logging.Log("[zonai-hookshot] CHAIN_HIDDEN reason=%u (2=not-tracked 4=not-published 8=old-generation 16=stale)", reason);
            hiddenReason = reason;
            ++hiddenDraws;
            out.visible=0;
        }
    }
    return true;
}

}  // namespace zonai_hookshot::render
