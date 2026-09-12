#include <lib.hpp>

#include "RecallEffectsEngine.hpp"
#include "RecallModelEngine.hpp"
#include "RecallRender.hpp"
#include "program/modules/self-recall/SelfRecallModule.hpp"

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
}

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

void fade(HandleSet& handle) {
    if (!g_mainBase || !valid(handle)) {
        handle = invalidHandleSet();
        return;
    }
    const auto fadeHandle = reinterpret_cast<void (*)(HandleSet*, int)>(
        g_mainBase + off::HandleSetFade);
    fadeHandle(&handle, -1);
    handle = invalidHandleSet();
}

void fade(Handle& handle) {
    if (!g_mainBase || !valid(handle)) {
        handle = invalidCompactHandle();
        return;
    }
    const auto fadeHandle = reinterpret_cast<void (*)(Handle*, int)>(
        g_mainBase + off::HandleFade);
    fadeHandle(&handle, -1);
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

}

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
    Logging.Log("[self-recall] PRESENT_START visual=%d wrist=%d audio=%d loop=%d",
                (int)visualStartOk, (int)wristLoopOk, (int)audioStartOk,
                (int)audioLoopOk);
    return g_active;
}

void stop(void* playerActor, const char* reason, bool emitEnd) {
    const bool wasActive = g_active || valid(g_startHandle) ||
                           valid(g_loopHandle) ||
                           valid(g_audioLoopHandle);
    wrist_effects::end();
    fade(g_endHandle);
    fade(g_audioLoopHandle);
    fade(g_loopHandle);
    fade(g_startHandle);
    g_active = false;

    bool visualEndOk = false;
    bool audioEndOk = false;
    if (emitEnd && playerActor) {
        visualEndOk =
            emitPlayer(playerActor, kVisualEndCue, 0, &g_endHandle);
        const ExpressionSoundRoute audio = resolveExpressionSound();
        Handle audioEndHandle = invalidCompactHandle();
        audioEndOk =
            emitAudio(audio.user, kAudioEndCue, &audioEndHandle);
    }
    if (wasActive || emitEnd) {
        Logging.Log(
            "[self-recall] PRESENT_STOP reason=%s visual_end=%d "
            "audio_end=%d",
            reason ? reason : "unspecified", (int)visualEndOk,
            (int)audioEndOk);
    }
}

void service() {
    if (g_endHandle.elink.poolIndex >= 0 && !valid(g_endHandle)) {
        g_endHandle = invalidHandleSet();
    }
}

}

namespace self_recall::wrist_effects {
namespace {
constexpr std::uintptr_t kCalcMatrix = 0x007C8B5C;
constexpr std::uintptr_t kHandleValid = 0x00D17C80;
constexpr std::uintptr_t kPoolBases = 0x0462F290;
constexpr std::uintptr_t kPoolStrides = 0x0462F298;
constexpr std::uintptr_t kUpdateEmitterMatrix = 0x0000FB4C;
constexpr std::uintptr_t kSetEmitterMatrix = 0x007C7E00;
constexpr std::uintptr_t kELinkSystemIndirect = 0x0462F2A8;
std::uintptr_t g_mainBase = 0;
pure::WristEffectOwners g_owners;
std::atomic<std::uint64_t> g_missingFrames{0};
pure::WristEmitterFrames<> g_emitters;
std::atomic<bool> g_haveEmitterBinding{false};
std::atomic<std::uint64_t> g_emitterRefusals{0};

template<class T> T read(const void* base, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
    return value;
}

void emitterRefused(unsigned reason) {
    const auto count = g_emitterRefusals.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 4 || count % 1800 == 0)
        Logging.Log("[self-recall] WRIST_EMITTER_REFUSED reason=%u count=%llu", reason,
            static_cast<unsigned long long>(count));
}

void bindEmitter(const void* executor, pure::WristEffectOwner owner,
                 const float wrist[12], const float effect[12]) {
    const auto* set = read<const void*>(executor, 0xB8);
    const auto instance = read<std::uint32_t>(executor, 0xC0);
    if (!set || read<std::uint32_t>(set, 0x234) != instance) return;
    pure::WristEmitterFrame frame{reinterpret_cast<std::uintptr_t>(set), instance, owner, {}};
    if (!pure::relativeEffectMatrix(wrist, effect, frame.local)) { emitterRefused(1); return; }
    if (!g_owners.current(owner.serial)) return;
    if (!g_emitters.put(frame)) { emitterRefused(2); return; }
    g_haveEmitterBinding.store(true, std::memory_order_relaxed);
}

void refreshEmitter(void* emitter) {
    if (!pose_session::active() || !g_haveEmitterBinding.load(std::memory_order_relaxed)) return;
    auto* set = read<void*>(emitter, 0);
    const auto binding = set ? g_emitters.get(reinterpret_cast<std::uintptr_t>(set),
        read<std::uint32_t>(set, 0x234)) : pure::WristEmitterFrame{};
    if (!binding.owner || !g_owners.current(binding.owner.serial)) return;
    pure::RenderWristFrame wrist;
    const auto* holder = read<const void*>(reinterpret_cast<const void*>(g_mainBase), kELinkSystemIndirect);
    const auto* system = holder ? read<const void*>(holder, 0) : nullptr;
    if (!system || !pose_render::copyWrist(binding.owner.historyGeneration, wrist)) {
        emitterRefused(3); return;
    }
    float origin[3];
    std::memcpy(origin, static_cast<const std::byte*>(system) + 0x64E74, sizeof(origin));
    alignas(16) float matrix[16];
    if (!pure::emitterMatrix(wrist.matrix, binding.local, origin, matrix)) {
        emitterRefused(4); return;
    }
    if (!g_owners.current(binding.owner.serial)) return;
    using SetMatrix = void (*)(void*, const float*);
    reinterpret_cast<SetMatrix>(g_mainBase + kSetEmitterMatrix)(set, matrix);
}

HOOK_DEFINE_TRAMPOLINE(UpdateEmitterMatrixHook) {
    static std::uint64_t Callback(void* emitter) {
        refreshEmitter(emitter);
        return Orig(emitter);
    }
};
pure::WristEffectBinding resolve(const pure::CompactEffectHandle& handle) {
    if (!g_mainBase || handle.type >= 0x80 || handle.poolIndex < 0) return {};
    using Valid = bool (*)(const pure::CompactEffectHandle*);
    if (!reinterpret_cast<Valid>(g_mainBase + kHandleValid)(&handle)) return {};
    const auto bases = read<const std::uintptr_t*>(reinterpret_cast<const void*>(g_mainBase), kPoolBases);
    const auto strides = read<const std::uintptr_t*>(reinterpret_cast<const void*>(g_mainBase), kPoolStrides);
    if (!bases || !strides) return {};
    const auto address = bases[handle.type] + strides[handle.type] * static_cast<unsigned>(handle.poolIndex);
    if (!address || read<std::uint32_t>(reinterpret_cast<const void*>(address), 0x20) != handle.eventId) return {};
    return {handle, address};
}

using NativeVector = float __attribute__((vector_size(16)));
HOOK_DEFINE_TRAMPOLINE(CalculateEffectMatrixHook) {
    static NativeVector Callback(void* executor, void* output, void* resource, void* user,
                                 void* argument4, const void* descriptor, unsigned flags) {
        const auto event = read<const void*>(executor, 0x18);
        const auto owner = event ? g_owners.match(reinterpret_cast<std::uintptr_t>(event),
                                                  read<std::uint32_t>(event, 0x20))
                                 : pure::WristEffectOwner{};
        if (!owner) {
            float matrix[12];
            if (!equipment_effects::copyMatrix(descriptor, matrix))
                return Orig(executor, output, resource, user, argument4, descriptor, flags);
            pure::WristEffectOwners scope;
            const auto serial = scope.publish(1, {});
            pure::WristMatrixProvider provider(scope, serial, matrix);
            const auto historical = provider.descriptor();
            return Orig(executor, output, resource, user, argument4, &historical, flags);
        }
        pure::RenderWristFrame frame;
        if (!pose_render::copyWrist(owner.historyGeneration, frame)) {
            const auto count = g_missingFrames.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count == 1 || count % 1800 == 0)
                Logging.Log("[self-recall] WRIST_FRAME_MISSING count=%llu owner=%llu history=%u",
                    static_cast<unsigned long long>(count), static_cast<unsigned long long>(owner.serial),
                    owner.historyGeneration);
            return Orig(executor, output, resource, user, argument4, descriptor, flags);
        }
        pure::WristMatrixProvider provider(g_owners, owner.serial, frame.matrix);
        const auto historical = provider.descriptor();
        const auto result = Orig(executor, output, resource, user, argument4, &historical, flags);
        if (provider.copied()) {
            bindEmitter(executor, owner, frame.matrix, static_cast<const float*>(output));
        }
        return result;
    }
};
}

void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    CalculateEffectMatrixHook::InstallAtOffset(kCalcMatrix);
    UpdateEmitterMatrixHook::InstallAtOffset(kUpdateEmitterMatrix);
}

void begin(std::uint32_t historyGeneration, std::span<const pure::CompactEffectHandle> handles) {
    g_owners.clear();
    g_emitters.clear();
    g_haveEmitterBinding.store(false, std::memory_order_relaxed);
    g_emitterRefusals.store(0, std::memory_order_relaxed);
    g_missingFrames.store(0, std::memory_order_relaxed);
    if (handles.size() > 2) {
        Logging.Log("[self-recall] WRIST_BIND_REFUSED handles=%u", static_cast<unsigned>(handles.size()));
        return;
    }
    pure::WristEffectBinding bindings[2];
    unsigned valid = 0;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        bindings[i] = resolve(handles[i]);
        valid += bindings[i].event != 0;
    }
    const auto serial = g_owners.publish(historyGeneration, {bindings, handles.size()});
    Logging.Log("[self-recall] WRIST_BIND owner=%llu history=%u resolved=%u/%u",
        static_cast<unsigned long long>(serial), historyGeneration, valid, static_cast<unsigned>(handles.size()));
}

void end() {
    g_owners.clear();
    g_emitters.clear();
    g_haveEmitterBinding.store(false, std::memory_order_relaxed);
    g_emitterRefusals.store(0, std::memory_order_relaxed);
    g_missingFrames.store(0, std::memory_order_relaxed);
}

}
