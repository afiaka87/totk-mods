// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#include <lib.hpp>
#include "Audio.hpp"

namespace audio {
namespace {

namespace off {

    constexpr ptrdiff_t SLinkSearchEmit   = 0x00B026E0;

    constexpr ptrdiff_t SearchAndEmitImpl = 0x009FFE64;

    constexpr ptrdiff_t SlinkSystemSlot   = 0x00462F2B0;
}

inline bool okPtr(u64 p) { return p >= 0x1000000 && p < 0x8000000000ull && (p & 0x3) == 0; }

inline bool streqBounded(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) { return false; }
    for (int i = 0; i < 64; i++) { if (a[i] != b[i]) { return false; } if (a[i] == '\0') { return true; } }
    return false;
}

uintptr_t g_mainBase = 0;

bool looksLikeSlinkInstance(u64 inst) {
    if (!okPtr(inst)) { return false; }
    const u64 mUser = *(u64*)(inst + 0x58);
    if (!okPtr(mUser)) { return false; }
    const u64 res = *(u64*)(mUser + 0x18);
    return okPtr(res);
}

void* g_uiSpeaker = nullptr;

void* findBankInstanceDirect() {
    const u64 slotAddr = g_mainBase + off::SlinkSystemSlot;
    const u64 holder = *(u64*)slotAddr;   if (!okPtr(holder)) { return nullptr; }
    const u64 system = *(u64*)holder;     if (!okPtr(system)) { return nullptr; }
    if (*(u32*)(system + 32) == 0) { return nullptr; }
    const int nodeOff = *(int*)(system + 36);
    const u64 anchor  = system + 16;
    u64 node = *(u64*)(system + 24);
    for (int guard = 0; guard < 4096 && node != anchor; guard++) {
        if (!okPtr(node)) { break; }
        const u64 user = node - (u64)nodeOff;
        const char* name = *(char**)(user + 16);
        if (okPtr((u64)name) && streqBounded(name, "UI_GlobalSound")) {
            if (*(int*)(user + 48) < 1) { return nullptr; }
            const u64 head = *(u64*)(user + 32);
            if (head == user + 32 || !okPtr(head)) { return nullptr; }
            const u64 inst = head - (u64)(*(int*)(user + 52));
            return looksLikeSlinkInstance(inst) ? (void*)inst : nullptr;
        }
        node = *(u64*)(node + 8);
    }
    return nullptr;
}

void* resolveUiGlobalSpeaker() {
    if (g_uiSpeaker != nullptr && looksLikeSlinkInstance((u64)g_uiSpeaker)) { return g_uiSpeaker; }
    void* inst = findBankInstanceDirect();
    if (inst != nullptr) {
        if (g_uiSpeaker == nullptr) { Logging.Log("[audio] BANK ACQUIRED (direct system walk) this=%p", inst); }
        g_uiSpeaker = inst;
    }
    return inst;
}

using SLinkSearchEmitFn = void (*)(void* userInstance, const char* name, void* out);

#if AUDIO_DEBUG_CAPTURE

constexpr const char* BANK_LOCK_CUES[] = {
    "PickUp_Default", "mc_HeartUp_Short", "mc_ExtraGanbari_Up", "mc_PlusMenuOpen",
};

u32 g_emitTotal   = 0;
u32 g_capUnique   = 0;
u32 g_bankLocks   = 0;

inline u32 djb2Hash(const char* s) {
    u32 h = 5381;
    for (int i = 0; i < 64 && s[i] != '\0'; i++) { h = (h * 33u) ^ (u8)(u8)s[i]; }
    return h;
}

inline bool nameEq(const char* a, const char* b) {
    if (a == nullptr) { return false; }
    for (int i = 0; i < 64; i++) { if (a[i] != b[i]) { return false; } if (a[i] == '\0') { return true; } }
    return false;
}

inline bool nameContains(const char* hay, const char* needle) {
    if (hay == nullptr || needle == nullptr) { return false; }
    for (int i = 0; i < 64 && hay[i] != '\0'; i++) {
        int j = 0;
        while (needle[j] != '\0' && hay[i + j] == needle[j]) { j++; }
        if (needle[j] == '\0') { return true; }
    }
    return false;
}

constexpr u32 CAPTURE_LOG_CAP = 2000;
u32 g_seenHashes[CAPTURE_LOG_CAP];

void captureEmit(void* thisptr, const char* name) {
    g_emitTotal++;
    if (name == nullptr) { return; }

    if (looksLikeSlinkInstance((u64)thisptr)) {
        for (int i = 0; i < (int)(sizeof(BANK_LOCK_CUES) / sizeof(BANK_LOCK_CUES[0])); i++) {
            if (nameEq(name, BANK_LOCK_CUES[i])) {
                const bool wasNew = (g_uiSpeaker == nullptr);
                g_uiSpeaker = thisptr;
                g_bankLocks++;
                if (wasNew) { Logging.Log("[audio] BANK LOCKED via '%s' this=%p", name, thisptr); }
                break;
            }
        }
    }

    if (nameContains(name, "BGM_EnvField") || nameContains(name, "BGM_FieldBattle")) { return; }
    if (g_capUnique < CAPTURE_LOG_CAP) {
        const u32 h = djb2Hash(name);
        bool seen = false;
        for (u32 i = 0; i < g_capUnique; i++) { if (g_seenHashes[i] == h) { seen = true; break; } }
        if (!seen) {
            g_seenHashes[g_capUnique++] = h;
            Logging.Log("[audio] CAP cue='%s' this=%p", name, thisptr);
        }
    }
}

HOOK_DEFINE_INLINE(SearchAndEmitImplHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        captureEmit((void*)ctx->X[0], (const char*)ctx->X[1]);
    }
};
#endif

}

void installHooks(uintptr_t mainBase) {
    g_mainBase = mainBase;
#if AUDIO_DEBUG_CAPTURE
    SearchAndEmitImplHook::InstallAtOffset(off::SearchAndEmitImpl);
    Logging.Log("[audio] capture hook installed @%p", (void*)(g_mainBase + off::SearchAndEmitImpl));
#endif
}

bool playCue(const char* cueName) {
    void* bank = resolveUiGlobalSpeaker();
    if (bank == nullptr) { return false; }
    u64 out[2] = {0, 0};
    auto emit = (SLinkSearchEmitFn)(g_mainBase + off::SLinkSearchEmit);
    emit(bank, cueName, out);
    Logging.Log("[audio] emit cue='%s' bank=%p", cueName, bank);
    return true;
}

bool bankLocked() { return g_uiSpeaker != nullptr && looksLikeSlinkInstance((u64)g_uiSpeaker); }

void primeBank() { resolveUiGlobalSpeaker(); }

#if AUDIO_DEBUG_CAPTURE
namespace debug {
    unsigned emitTotal()      { return g_emitTotal; }
    unsigned uniqueCueCount() { return g_capUnique; }
    unsigned bankLockCount()  { return g_bankLocks; }
}
#endif

}
