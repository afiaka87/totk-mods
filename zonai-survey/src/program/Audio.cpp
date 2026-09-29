// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#include <lib.hpp>
#include "Audio.hpp"
#include "SurveyGameProfiles.hpp"

namespace audio {
namespace {

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
bool g_reportedMissing = false;

void* findBankInstanceDirect() {
    const u64 variable = g_mainBase + zonai_survey::profiles::active->variables.soundSystem;
    const u64 system = *(u64*)variable;   if (!okPtr(system)) { return nullptr; }
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
    if (inst != nullptr) { g_uiSpeaker = inst; }
    return inst;
}

using SLinkSearchEmitFn = void (*)(void* userInstance, const char* name, void* out);

}

void init(uintptr_t mainBase) {
    g_mainBase = mainBase;
}

void playCue(const char* cueName) {
    void* bank = resolveUiGlobalSpeaker();
    if (bank == nullptr) {
        if (!g_reportedMissing) { Logging.Log("[audio] UI sound bank not found; survey cues are silent"); }
        g_reportedMissing = true;
        return;
    }
    u64 out[2] = {0, 0};
    auto emit = (SLinkSearchEmitFn)(g_mainBase + zonai_survey::profiles::active->calls.searchAndEmit);
    emit(bank, cueName, out);
}

}
