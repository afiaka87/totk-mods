// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "CampAudio.hpp"

#include <lib.hpp>

namespace bivouac::audio {
namespace {

constexpr std::ptrdiff_t kSearchAndEmitOffset = 0x00B026E0;
constexpr std::ptrdiff_t kSoundSystemSlotOffset = 0x00462F2B0;
constexpr std::ptrdiff_t kHandleIsValidOffset = 0x00D17C80;

using SearchAndEmitFn = void (*)(void* user, const char* name, void* outHandle);
using HandleIsValidFn = bool (*)(const void* handle);

constexpr const char* kInterfaceUser = "UI_GlobalSound";

std::uintptr_t g_mainBase = 0;
void* g_speaker = nullptr;

inline bool okPtr(u64 p) {
    return p >= 0x1000000 && p < 0x8000000000ull && (p & 0x3) == 0;
}

inline bool nameIs(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return false;
    for (int i = 0; i < 64; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return false;
}

bool looksLikeInstance(u64 inst) {
    if (!okPtr(inst)) return false;
    const u64 user = *(u64*)(inst + 0x58);
    if (!okPtr(user)) return false;
    return okPtr(*(u64*)(user + 0x18));
}

u64 findUser(const char* userName) {
    if (g_mainBase == 0) return 0;
    const u64 holder = *(u64*)(g_mainBase + kSoundSystemSlotOffset);
    if (!okPtr(holder)) return 0;
    const u64 system = *(u64*)holder;
    if (!okPtr(system)) return 0;
    if (*(u32*)(system + 32) == 0) return 0;

    const int nodeOff = *(int*)(system + 36);
    const u64 anchor = system + 16;
    u64 node = *(u64*)(system + 24);
    for (int guard = 0; guard < 4096 && node != anchor; guard++) {
        if (!okPtr(node)) break;
        const u64 user = node - (u64)nodeOff;
        const char* name = *(char**)(user + 16);
        if (okPtr((u64)name) && nameIs(name, userName)) return user;
        node = *(u64*)(node + 8);
    }
    return 0;
}

void* firstInstance(u64 user) {
    if (user == 0 || *(int*)(user + 48) < 1) return nullptr;
    const u64 head = *(u64*)(user + 32);
    if (head == user + 32 || !okPtr(head)) return nullptr;
    const u64 inst = head - (u64)(*(int*)(user + 52));
    return looksLikeInstance(inst) ? (void*)inst : nullptr;
}

void* speaker() {
    if (g_speaker != nullptr && looksLikeInstance((u64)g_speaker)) return g_speaker;
    void* found = firstInstance(findUser(kInterfaceUser));
    if (found != nullptr) {
        if (g_speaker == nullptr) Logging.Log("[bv] audio: speaker found %p", found);
        g_speaker = found;
    }
    return found;
}

bool emit(void* instance, const char* cueName) {
    if (instance == nullptr || cueName == nullptr || g_mainBase == 0) return false;
    // Handle bytes 2-3 are the sound index; 0xFFFF means no sound and must be the start state.
    std::uint8_t handle[16] = {0, 0, 0xFF, 0xFF};
    auto searchAndEmit = (SearchAndEmitFn)(g_mainBase + kSearchAndEmitOffset);
    searchAndEmit(instance, cueName, handle);
    auto isValid = (HandleIsValidFn)(g_mainBase + kHandleIsValidOffset);
    return isValid(handle);
}

}  // namespace

void initialize(std::uintptr_t mainBase) { g_mainBase = mainBase; }

void prime() { speaker(); }

bool ready() { return g_speaker != nullptr && looksLikeInstance((u64)g_speaker); }

bool playCue(const char* cueName) { return emit(speaker(), cueName); }

}  // namespace bivouac::audio
