#include <lib.hpp>

#include "RecallPresentation.hpp"
#include "RecallWristEffects.hpp"

namespace self_recall::presentation {
namespace {

namespace off {
constexpr ptrdiff_t ActorGetXLink = 0x00BBD0F4;
constexpr ptrdiff_t ComponentSearchAndEmit = 0x00BBC7C0;
constexpr ptrdiff_t HandleSetIsValid = 0x00FC2C48;
constexpr ptrdiff_t HandleSetFade = 0x00B94C7C;
constexpr ptrdiff_t ResolveSoundPresenter = 0x00FF7C38;
constexpr ptrdiff_t SLinkSearchAndEmit = 0x00B026E0;
constexpr ptrdiff_t HandleIsValid = 0x00D17C80;
constexpr ptrdiff_t HandleFade = 0x00AF78BC;
}  // namespace off

using Handle = pure::CompactEffectHandle;
static_assert(sizeof(Handle) == 0x08);

struct alignas(8) HandleSet {
    Handle elink;
    Handle slink;
};
static_assert(sizeof(HandleSet) == 0x10);

constexpr const char* kVisualStartCue = "SplPwr_Start_Modoreco";
constexpr const char* kWristLoopCue = "Modoreco_Ready";
constexpr const char* kVisualEndCue = "Modoreco_End";
constexpr const char* kAudioStartCue = "ReverseRecorder_Start";
constexpr const char* kAudioLoopCue = "ReverseRecorder_Lp";
constexpr const char* kAudioEndCue = "ReverseRecorder_End";

std::uintptr_t g_mainBase = 0;
Handle invalidCompactHandle() {
    Handle handle{};
    handle.type = 0xff;
    handle.padding = 0;
    handle.poolIndex = -1;
    handle.eventId = 0;
    return handle;
}

HandleSet invalidHandleSet() {
    return {invalidCompactHandle(), invalidCompactHandle()};
}

HandleSet g_startHandle = invalidHandleSet();
HandleSet g_loopHandle = invalidHandleSet();
HandleSet g_endHandle = invalidHandleSet();
Handle g_audioLoopHandle = invalidCompactHandle();
bool g_active = false;

bool okPtr(std::uintptr_t pointer) {
    return pointer >= 0x1000 && (pointer & 7) == 0 &&
           pointer < (1ull << 40);
}

bool okCodePtr(std::uintptr_t pointer) {
    return pointer >= 0x1000 && (pointer & 3) == 0 &&
           pointer < (1ull << 40);
}

bool valid(const HandleSet& handle) {
    if (!g_mainBase) return false;
    const auto isValid = reinterpret_cast<bool (*)(const HandleSet*)>(
        g_mainBase + off::HandleSetIsValid);
    return isValid(&handle);
}

bool valid(const Handle& handle) {
    if (!g_mainBase) return false;
    const auto isValid = reinterpret_cast<bool (*)(const Handle*)>(
        g_mainBase + off::HandleIsValid);
    return isValid(&handle);
}

void fade(HandleSet& handle, const char* label) {
    if (!g_mainBase || !valid(handle)) {
        handle = invalidHandleSet();
        return;
    }
    const auto fadeHandle = reinterpret_cast<void (*)(HandleSet*, int)>(
        g_mainBase + off::HandleSetFade);
    fadeHandle(&handle, -1);
    Logging.Log("[self-recall] PRESENT_VISUAL_FADE cue=%s", label);
    handle = invalidHandleSet();
}

void fade(Handle& handle, const char* label) {
    if (!g_mainBase || !valid(handle)) {
        handle = invalidCompactHandle();
        return;
    }
    const auto fadeHandle = reinterpret_cast<void (*)(Handle*, int)>(
        g_mainBase + off::HandleFade);
    fadeHandle(&handle, -1);
    Logging.Log(
        "[self-recall] PRESENT_AUDIO_FADE cue=%s type=%u pool=%d event=%u",
        label, (unsigned)handle.type, (int)handle.poolIndex,
        handle.eventId);
    handle = invalidCompactHandle();
}

bool emitPlayer(void* playerActor, const char* cueName, int type,
                HandleSet* out) {
    if (!g_mainBase || !okPtr(reinterpret_cast<std::uintptr_t>(playerActor)) ||
        !cueName || !out)
        return false;

    const auto getXLink = reinterpret_cast<void* (*)(void*)>(
        g_mainBase + off::ActorGetXLink);
    const auto searchAndEmit =
        reinterpret_cast<void (*)(void*, const char**, HandleSet*, int)>(
            g_mainBase + off::ComponentSearchAndEmit);
    void* component = getXLink(playerActor);
    if (!okPtr(reinterpret_cast<std::uintptr_t>(component))) return false;

    *out = invalidHandleSet();
    const char* cue = cueName;
    searchAndEmit(component, &cue, out, type);
    return valid(*out);
}

struct ExpressionSoundRoute {
    void* presenter;
    void* wrapper;
    void* user;
};

ExpressionSoundRoute resolveExpressionSound() {
    ExpressionSoundRoute route{};
    if (!g_mainBase) return route;

    const auto resolvePresenter = reinterpret_cast<void* (*)()>(
        g_mainBase + off::ResolveSoundPresenter);
    route.presenter = resolvePresenter();
    if (!okPtr(reinterpret_cast<std::uintptr_t>(route.presenter))) {
        route.presenter = nullptr;
        return route;
    }

    route.wrapper = *reinterpret_cast<void**>(
        reinterpret_cast<std::uintptr_t>(route.presenter) + 0x138);
    if (!okPtr(reinterpret_cast<std::uintptr_t>(route.wrapper))) {
        route.wrapper = nullptr;
        return route;
    }

    const std::uintptr_t vtable =
        *reinterpret_cast<const std::uintptr_t*>(route.wrapper);
    if (!okPtr(vtable)) return route;
    const std::uintptr_t getUser =
        *reinterpret_cast<const std::uintptr_t*>(vtable + 0x20);
    if (!okCodePtr(getUser)) return route;

    route.user =
        reinterpret_cast<void* (*)(void*)>(getUser)(route.wrapper);
    if (!okPtr(reinterpret_cast<std::uintptr_t>(route.user)))
        route.user = nullptr;
    return route;
}

bool emitAudio(void* user, const char* cueName, Handle* out) {
    if (!g_mainBase || !okPtr(reinterpret_cast<std::uintptr_t>(user)) ||
        !cueName || !out)
        return false;
    *out = invalidCompactHandle();
    const auto searchAndEmit =
        reinterpret_cast<void (*)(void*, const char*, Handle*)>(
            g_mainBase + off::SLinkSearchAndEmit);
    searchAndEmit(user, cueName, out);
    return valid(*out);
}

void logVisualLanes(const char* label, const HandleSet& handle) {
    Logging.Log(
        "[self-recall] PRESENT_VISUAL_HANDLE cue=%s "
        "elink=(type=%u pool=%d event=%u valid=%d) "
        "slink=(type=%u pool=%d event=%u valid=%d)",
        label, (unsigned)handle.elink.type, (int)handle.elink.poolIndex,
        handle.elink.eventId, (int)valid(handle.elink),
        (unsigned)handle.slink.type, (int)handle.slink.poolIndex,
        handle.slink.eventId, (int)valid(handle.slink));
}

}  // namespace

void initialize(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    g_startHandle = invalidHandleSet();
    g_loopHandle = invalidHandleSet();
    g_endHandle = invalidHandleSet();
    g_audioLoopHandle = invalidCompactHandle();
    g_active = false;
    wrist_effects::install(mainBase);
}

bool start(void* playerActor, std::uint32_t historyGeneration) {
    stop(playerActor, "restart", false);

    const bool visualStartOk =
        emitPlayer(playerActor, kVisualStartCue, 0, &g_startHandle);
    const bool wristLoopOk =
        emitPlayer(playerActor, kWristLoopCue, 0, &g_loopHandle);
    logVisualLanes(kVisualStartCue, g_startHandle);
    logVisualLanes(kWristLoopCue, g_loopHandle);
    const Handle ownedEffects[]{g_startHandle.elink, g_loopHandle.elink};
    wrist_effects::begin(historyGeneration, ownedEffects);

    const ExpressionSoundRoute audio = resolveExpressionSound();
    Handle audioStartHandle = invalidCompactHandle();
    const bool audioStartOk =
        emitAudio(audio.user, kAudioStartCue, &audioStartHandle);
    const bool audioLoopOk =
        emitAudio(audio.user, kAudioLoopCue, &g_audioLoopHandle);

    g_active =
        visualStartOk || wristLoopOk || audioStartOk || audioLoopOk;
    Logging.Log(
        "[self-recall] PRESENT_START visual_start=%d wrist_loop=%d "
        "audio_start=%d audio_loop=%d active=%d "
        "presenter=%p wrapper=%p user=%p loop=(type=%u pool=%d event=%u)",
        (int)visualStartOk, (int)wristLoopOk, (int)audioStartOk,
        (int)audioLoopOk, (int)g_active, audio.presenter, audio.wrapper,
        audio.user, (unsigned)g_audioLoopHandle.type,
        (int)g_audioLoopHandle.poolIndex, g_audioLoopHandle.eventId);
    return g_active;
}

void stop(void* playerActor, const char* reason, bool emitEnd) {
    const bool wasActive = g_active || valid(g_startHandle) ||
                           valid(g_loopHandle) ||
                           valid(g_audioLoopHandle);
    wrist_effects::end();
    fade(g_endHandle, kVisualEndCue);
    fade(g_audioLoopHandle, kAudioLoopCue);
    fade(g_loopHandle, kWristLoopCue);
    fade(g_startHandle, kVisualStartCue);
    g_active = false;

    bool visualEndOk = false;
    bool audioEndOk = false;
    if (emitEnd && playerActor) {
        visualEndOk =
            emitPlayer(playerActor, kVisualEndCue, 0, &g_endHandle);
        logVisualLanes(kVisualEndCue, g_endHandle);

        const ExpressionSoundRoute audio = resolveExpressionSound();
        Handle audioEndHandle = invalidCompactHandle();
        audioEndOk =
            emitAudio(audio.user, kAudioEndCue, &audioEndHandle);
        Logging.Log(
            "[self-recall] PRESENT_END_ROUTE presenter=%p wrapper=%p "
            "user=%p audio=(type=%u pool=%d event=%u valid=%d)",
            audio.presenter, audio.wrapper, audio.user,
            (unsigned)audioEndHandle.type, (int)audioEndHandle.poolIndex,
            audioEndHandle.eventId, (int)audioEndOk);
    }
    if (wasActive || emitEnd) {
        Logging.Log(
            "[self-recall] PRESENT_STOP reason=%s visual_end=%d "
            "audio_end=%d",
            reason ? reason : "unspecified", (int)visualEndOk,
            (int)audioEndOk);
    }
}

bool active() { return g_active; }

void service() {
    if (g_endHandle.elink.poolIndex >= 0 && !valid(g_endHandle)) {
        Logging.Log("[self-recall] PRESENT_END_RETIRED event=%u", g_endHandle.elink.eventId);
        g_endHandle = invalidHandleSet();
    }
}

}  // namespace self_recall::presentation
