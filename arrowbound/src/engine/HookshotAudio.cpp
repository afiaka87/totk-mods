// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "HookshotAudio.hpp"
#include "ArrowboundCues.hpp"
#include "SoundHandle.hpp"

#include <lib.hpp>

namespace arrowbound::audio {
namespace {

namespace off {
// Hand a cue name to a sound user instance and play it.
constexpr std::ptrdiff_t kSearchAndEmit = 0x00B026E0;
// The slot holding the sound system whose list carries every registered user.
constexpr std::ptrdiff_t kSystemSlot = 0x00462F2B0;
constexpr std::ptrdiff_t kHandleIsValid = 0x00D17C80;
constexpr std::ptrdiff_t kHandleAliveAssets = 0x01A00080;
constexpr std::ptrdiff_t kSetPosition = 0x00829F60;
constexpr std::ptrdiff_t kKill = 0x00BBCCAC;
constexpr std::ptrdiff_t kFade = 0x00AF78BC;
}  // namespace off

using SearchAndEmitFn = void (*)(void* user, const char* name, void* outHandle);
using HandleIsValidFn = bool (*)(const void* handle);

constexpr const char* kInterfaceUser = "UI_GlobalSound";

std::uintptr_t g_mainBase = 0;
void* g_speaker = nullptr;

struct AbilityCue {
    engine::SoundHandle handle{};
    pure::AbilityCueGate gate{};
    const char* kind = nullptr;
    const char* fallback = nullptr;
    bool active = false;
};
AbilityCue g_abilityCues[2]{};
engine::SoundHandle g_arrowFollowLoop{};

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

// A user instance we can emit through has a resource behind it.
bool looksLikeInstance(u64 inst) {
    if (!okPtr(inst)) return false;
    const u64 user = *(u64*)(inst + 0x58);
    if (!okPtr(user)) return false;
    return okPtr(*(u64*)(user + 0x18));
}

// The registered-user list node for a name, or 0.
u64 findUser(const char* userName) {
    if (g_mainBase == 0) return 0;
    const u64 holder = *(u64*)(g_mainBase + off::kSystemSlot);
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

// The first live instance hanging off a user, or nullptr.
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
        if (g_speaker == nullptr) Logging.Log("[audio] speaker found %p", found);
        g_speaker = found;
    }
    return found;
}

bool emit(void* instance, const char* cueName, engine::SoundHandle* retained = nullptr) {
    if (instance == nullptr || cueName == nullptr || g_mainBase == 0) return false;
    // Bytes 2-3 are the sound index; -1 means "no sound" and must be the
    // starting state, since index 0 is someone else's real sound.
    engine::SoundHandle handle{};
    auto searchAndEmit = (SearchAndEmitFn)(g_mainBase + off::kSearchAndEmit);
    searchAndEmit(instance, cueName, &handle);
    if (retained) *retained = handle;
    auto isValid = (HandleIsValidFn)(g_mainBase + off::kHandleIsValid);
    return isValid(&handle);
}

bool validHandle(const engine::SoundHandle& handle) {
    return g_mainBase && handle.system == 1 && handle.index >= 0 &&
        reinterpret_cast<HandleIsValidFn>(g_mainBase + off::kHandleIsValid)(&handle);
}

void stop(AbilityCue& cue) {
    if (cue.active && validHandle(cue.handle))
        reinterpret_cast<void (*)(engine::SoundHandle*)>(g_mainBase + off::kKill)(&cue.handle);
    cue = {};
}

void update(AbilityCue& cue, pure::Vec3 position) {
    if (!cue.active) return;
    const bool event = validHandle(cue.handle);
    unsigned assets = 0;
    if (event) {
        if (pure::finite3(position))
            reinterpret_cast<void (*)(engine::SoundHandle*, const pure::Vec3*)>(
                g_mainBase + off::kSetPosition)(&cue.handle, &position);
        assets = reinterpret_cast<unsigned (*)(const engine::SoundHandle*)>(
            g_mainBase + off::kHandleAliveAssets)(&cue.handle);
    }
    const bool wasResolved = cue.gate.resolved;
    if (cue.gate.needsFallback(event, assets)) {
        const char* fallback = cue.fallback;
        const char* kind = cue.kind;
        stop(cue); // Empty pending events cannot play late on top of the fallback.
        const bool emitted = emit(speaker(), fallback);
        Logging.Log("[arrowbound] AUDIO_FALLBACK kind=%s cue=%s result=%u reason=no_live_asset",
                    kind, fallback, (unsigned)emitted);
    } else if (!wasResolved && cue.gate.resolved) {
        Logging.Log("[arrowbound] AUDIO_ASSET kind=%s live=%u follows_player=1", cue.kind, assets);
    } else if (!event) {
        cue = {};
    }
}

void updateArrowFollowLoop(pure::Vec3 position) {
    if (!validHandle(g_arrowFollowLoop)) {
        g_arrowFollowLoop = {};
        return;
    }
    if (pure::finite3(position))
        reinterpret_cast<void (*)(engine::SoundHandle*, const pure::Vec3*)>(
            g_mainBase + off::kSetPosition)(&g_arrowFollowLoop, &position);
}

}  // namespace

void initialize(std::uintptr_t mainBase) { g_mainBase = mainBase; }

void prime() { speaker(); }

bool ready() { return g_speaker != nullptr && looksLikeInstance((u64)g_speaker); }

bool playCue(const char* cueName) { return emit(speaker(), cueName); }

bool playCue(const char* userName, const char* cueName) {
    return emit(firstInstance(findUser(userName)), cueName);
}

void playAbilityCue(bool arrival, pure::Vec3 position) {
    using namespace pure;
    auto& cue = g_abilityCues[arrival ? 1 : 0];
    stop(cue);
    cue.kind = arrival ? "arrive" : "travel";
    cue.fallback = arrival ? kCueArriveFallback : kCueTravelFallback;
    const char* name = arrival ? kCueArrive : kCueTravel;
    const bool allocated = emit(firstInstance(findUser(kCueAbilityUser)), name, &cue.handle);
    cue.active = true;
    Logging.Log("[arrowbound] AUDIO_REQUEST kind=%s cue=%s event=%u", cue.kind, name, (unsigned)allocated);
    update(cue, position);
}

void startArrowFollowLoop(pure::Vec3 position) {
    stopArrowFollowLoop();
    const bool allocated = emit(firstInstance(findUser(pure::kCueAbilityUser)),
                                pure::kCueArrowFollowLoop, &g_arrowFollowLoop);
    Logging.Log("[arrowbound] AUDIO_REQUEST kind=arrow_follow cue=%s event=%u",
                pure::kCueArrowFollowLoop, (unsigned)allocated);
    updateArrowFollowLoop(position);
}

void stopArrowFollowLoop() {
    if (validHandle(g_arrowFollowLoop))
        reinterpret_cast<void (*)(engine::SoundHandle*, int)>(
            g_mainBase + off::kFade)(&g_arrowFollowLoop, -1);
    g_arrowFollowLoop = {};
}

void updateAbilityCues(pure::Vec3 position) {
    for (auto& cue : g_abilityCues) update(cue, position);
    updateArrowFollowLoop(position);
}

void resetAbilityCues() {
    for (auto& cue : g_abilityCues) stop(cue);
    stopArrowFollowLoop();
}

bool describeUser(const char* userName, int& instances) {
    const u64 user = findUser(userName);
    instances = user != 0 ? *(int*)(user + 48) : 0;
    return user != 0;
}

}  // namespace arrowbound::audio
