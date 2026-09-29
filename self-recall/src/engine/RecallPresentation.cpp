#include "totk/engine/ReadGuard.hpp"
#include <lib.hpp>
#include <cmath>

#include "GameProfiles.hpp"
#include "RecallEffectsEngine.hpp"
#include "RecallGraphicsEngine.hpp"
#include "RecallModelEngine.hpp"
#include "RecallRender.hpp"
#include "program/modules/self-recall/SelfRecallModule.hpp"

namespace self_recall::presentation_sites {
struct Sites {
    std::ptrdiff_t actorGetXLink, componentSearchEmit, handleSetValid, handleSetFade;
    std::ptrdiff_t resolveSound, slinkEmit, handleValid, handleFade;
    std::ptrdiff_t calcMatrix, poolBases, poolStrides, updateEmitter;
    std::ptrdiff_t setEmitter, elinkSystem;
    std::uint32_t calcWord, updateWord;
};

constexpr std::array<Sites, 9> kSites{{
    {0x11D1F90, 0xB5BDE8, 0xFC0C28, 0xA8D550, 0xFDD210, 0xAC72EC,
     0xCB9B78, 0xABC568, 0x730EB4, 0x4558A98, 0x4558AA0, 0xFB4C,
     0x730158, 0x4558AB0, 0xD10683FF, 0xD100C3FF},
    {0xBB3928, 0xBB2D34, 0xFF02B4, 0xB57CFC, 0x10088E0, 0xB2D620,
     0xD1B63C, 0xB226A4, 0x7ACAC0, 0x4631B80, 0x4631B88, 0xFB4C,
     0x7ABD64, 0x4631B98, 0xD10643FF, 0xD100C3FF},
    {0xB912CC, 0xB906E8, 0xFD4008, 0xB4B630, 0x10258B0, 0xB1FB64,
     0xD8A388, 0xB14C2C, 0x7B25D8, 0x462BEE8, 0x462BEF0, 0xFB4C,
     0x7B187C, 0x462BF00, 0xD10683FF, 0xD100C3FF},
    {0xB2C5DC, 0xB2BE44, 0xFD93D4, 0xA8EC84, 0xFDF214, 0xA63CE4,
     0xDAE878, 0xA582B4, 0x788B94, 0x46202B8, 0x46202C0, 0xFB4C,
     0x787E38, 0x46202D0, 0xD10643FF, 0xD100C3FF},
    {0xBBD0F4, 0xBBC7C0, 0xFC2C48, 0xB94C7C, 0xFF7C38, 0xB026E0,
     0xD17C80, 0xAF78BC, 0x7C8B5C, 0x462F290, 0x462F298, 0xFB4C,
     0x7C7E00, 0x462F2A8, 0xD10683FF, 0xD100C3FF},
    {0xB3D880, 0x72F004, 0xB2ED5C, 0x7DDBE8, 0xC75190, 0xA96364,
     0xD38D4, 0xADC1F0, 0x28D25F4, 0x3950388, 0x3950390, 0x191390,
     0x27A56A8, 0x3950B78, 0xD10603FF, 0xB9403008},
    {0xB29718, 0xC4AF08, 0x94DBC, 0x8183DC, 0xC7D274, 0xA89840,
     0x54564C, 0xAD4080, 0x28CA354, 0x394B388, 0x394B390, 0x1772E4,
     0x2799568, 0x394BB78, 0xD10603FF, 0xB9403008},
    {0xB1CF18, 0xC24FFC, 0xB1071C, 0x82D57C, 0xC88B4C, 0xAC1D48,
     0x55F048, 0xAC8618, 0x28CA0F0, 0x394D388, 0x394D390, 0xEBC10,
     0x279A0E8, 0x394DB70, 0xD10603FF, 0xB9403008},
    {0xB03CC8, 0x6D24A4, 0xB9B684, 0x6E34E8, 0xCBFA20, 0xA98D00,
     0x481918, 0xAB5B6C, 0x28DA598, 0x395F388, 0x395F390, 0x9EA50,
     0x27AAD18, 0x395FB70, 0xD10603FF, 0xB9403008},
}};

const Sites* active() { return &profiles::row(kSites); }

bool valid(std::uintptr_t base) {
    const auto& sites = *active();
    const auto textSize = exl::util::GetMainModuleInfo().m_Text.m_Size;
    return profiles::holds(base, textSize, sites.calcMatrix, sites.calcWord) &&
           profiles::holds(base, textSize, sites.updateEmitter, sites.updateWord);
}
}

namespace self_recall::presentation {
namespace {

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
const presentation_sites::Sites* g_sites = nullptr;
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
        g_mainBase + g_sites->handleSetValid);
    return isValid(&handle);
}

bool valid(const Handle& handle) {
    if (!g_mainBase) return false;
    const auto isValid = reinterpret_cast<bool (*)(const Handle*)>(
        g_mainBase + g_sites->handleValid);
    return isValid(&handle);
}

void fade(HandleSet& handle) {
    if (!g_mainBase || !valid(handle)) {
        handle = invalidHandleSet();
        return;
    }
    const auto fadeHandle = reinterpret_cast<void (*)(HandleSet*, int)>(
        g_mainBase + g_sites->handleSetFade);
    fadeHandle(&handle, -1);
    handle = invalidHandleSet();
}

void fade(Handle& handle) {
    if (!g_mainBase || !valid(handle)) {
        handle = invalidCompactHandle();
        return;
    }
    const auto fadeHandle = reinterpret_cast<void (*)(Handle*, int)>(
        g_mainBase + g_sites->handleFade);
    fadeHandle(&handle, -1);
    handle = invalidCompactHandle();
}

bool emitPlayer(void* playerActor, const char* cueName, int type,
                HandleSet* out) {
    if (!g_mainBase || !okPtr(reinterpret_cast<std::uintptr_t>(playerActor)) ||
        !cueName || !out)
        return false;

    const auto getXLink = reinterpret_cast<void* (*)(void*)>(
        g_mainBase + g_sites->actorGetXLink);
    const auto searchAndEmit =
        reinterpret_cast<void (*)(void*, const char**, HandleSet*, int)>(
            g_mainBase + g_sites->componentSearchEmit);
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
        g_mainBase + g_sites->resolveSound);
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
            g_mainBase + g_sites->slinkEmit);
    searchAndEmit(user, cueName, out);
    return valid(*out);
}

}

void initialize(std::uintptr_t mainBase) {
    g_sites = presentation_sites::valid(mainBase) ? presentation_sites::active() : nullptr;
    g_mainBase = g_sites ? mainBase : 0;
    if (!g_sites)
        Logging.Log("[self-recall] presentation hook sites differ from selected game build");
    g_startHandle = invalidHandleSet();
    g_loopHandle = invalidHandleSet();
    g_endHandle = invalidHandleSet();
    g_audioLoopHandle = invalidCompactHandle();
    g_active = false;
    wrist_effects::install(mainBase);
}

bool start(void* playerActor, std::uint32_t historyGeneration) {
    stop(playerActor, false);

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

    g_active = visualStartOk || wristLoopOk || audioStartOk || audioLoopOk;
    return g_active;
}

void stop(void* playerActor, bool emitEnd) {
    wrist_effects::end();
    fade(g_endHandle);
    fade(g_audioLoopHandle);
    fade(g_loopHandle);
    fade(g_startHandle);
    g_active = false;

    if (emitEnd && playerActor) {
        emitPlayer(playerActor, kVisualEndCue, 0, &g_endHandle);
        Handle audioEndHandle = invalidCompactHandle();
        emitAudio(resolveExpressionSound().user, kAudioEndCue, &audioEndHandle);
    }
}

void service() {
    wrist_effects::serviceBootstrap();
    if (g_endHandle.elink.poolIndex >= 0 && !valid(g_endHandle)) {
        g_endHandle = invalidHandleSet();
    }
}

}

namespace self_recall::wrist_effects {
namespace {
std::uintptr_t g_mainBase = 0;
const presentation_sites::Sites* g_sites = nullptr;
pure::WristEffectOwners g_owners;
std::atomic<std::uint64_t> g_missingFrames{0};
std::array<std::atomic<std::uintptr_t>, 2> g_expectedEvents{};
pure::WristEmitterFrames<32> g_emitters;
std::atomic<bool> g_haveEmitterBinding{false};
std::atomic<std::uint64_t> g_emitterRefusals{0};
constexpr std::array<std::ptrdiff_t, 9> kFindExecutor{{
    0, 0, 0, 0, 0, 0x28D441C, 0x28CC140, 0x28CBEDC, 0x28DC384,
}};
constexpr std::uint32_t kFindExecutorWord = 0xA9BD7BFD;
constexpr std::uint32_t kSetEmitterWord = 0xA9BE7BFD;
std::ptrdiff_t g_findExecutor = 0;
std::array<pure::CompactEffectHandle, 2> g_bootstrapHandles{};
std::array<unsigned, 2> g_bootstrapTries{};
std::array<bool, 2> g_bootstrapDone{};
float g_bootstrapInitialWrist[12]{};
bool g_bootstrapHaveInitialWrist = false;
bool g_bootstrapEnabled = false;

template<class T> T read(const void* base, std::size_t offset) {
    if (!totk::engine::read_guard::admit(base, offset, sizeof(T))) return T{};
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

bool bindEmitter(const void* executor, pure::WristEffectOwner owner,
                 const float wrist[12], const float effect[12],
                 const void* anchorUnit = nullptr, unsigned anchorBone = 0) {
    const auto* set = read<const void*>(executor, 0xB8);
    const auto instance = read<std::uint32_t>(executor, 0xC0);
    if (!set || read<std::uint32_t>(set, 0x234) != instance) return false;
    pure::WristEmitterFrame frame{reinterpret_cast<std::uintptr_t>(set), instance, owner, {},
                                  reinterpret_cast<std::uintptr_t>(anchorUnit), anchorBone};
    if (!pure::relativeEffectMatrix(wrist, effect, frame.local)) { emitterRefused(1); return false; }
    if (!g_owners.current(owner.serial)) return false;
    if (!g_emitters.put(frame)) { emitterRefused(2); return false; }
    g_haveEmitterBinding.store(true, std::memory_order_relaxed);
    return true;
}

void refreshEmitter(void* emitter) {
    if (!g_sites || !pose_session::active() || !g_haveEmitterBinding.load(std::memory_order_relaxed)) return;
    auto* set = read<void*>(emitter, 0);
    const auto binding = set ? g_emitters.get(reinterpret_cast<std::uintptr_t>(set),
        read<std::uint32_t>(set, 0x234)) : pure::WristEmitterFrame{};
    if (!binding.owner || !g_owners.current(binding.owner.serial)) return;
    const auto* holder = read<const void*>(reinterpret_cast<const void*>(g_mainBase), g_sites->elinkSystem);
    const auto* system = holder ? read<const void*>(holder, 0) : nullptr;
    if (!system) { emitterRefused(3); return; }
    float anchor[12];
    if (binding.anchorUnit) {
        if (!pose_render::copyBone(reinterpret_cast<const void*>(binding.anchorUnit), binding.anchorBone, anchor)) {
            emitterRefused(5); return;
        }
    } else {
        pure::RenderWristFrame wrist;
        if (!pose_render::copyWrist(binding.owner.historyGeneration, wrist)) { emitterRefused(3); return; }
        std::memcpy(anchor, wrist.matrix, sizeof(anchor));
    }
    float origin[3];
    std::memcpy(origin, static_cast<const std::byte*>(system) + 0x64E74, sizeof(origin));
    alignas(16) float matrix[16];
    if (!pure::emitterMatrix(anchor, binding.local, origin, matrix)) {
        emitterRefused(4); return;
    }
    if (!g_owners.current(binding.owner.serial)) return;
    using SetMatrix = void (*)(void*, const float*);
    reinterpret_cast<SetMatrix>(g_mainBase + g_sites->setEmitter)(set, matrix);
}

HOOK_DEFINE_TRAMPOLINE(UpdateEmitterMatrixHook) {
    static std::uint64_t Callback(void* emitter) {
        refreshEmitter(emitter);
        return Orig(emitter);
    }
};
pure::WristEffectBinding resolve(const pure::CompactEffectHandle& handle) {
    if (!g_mainBase || !g_sites || handle.type >= 0x80 || handle.poolIndex < 0) return {};
    using Valid = bool (*)(const pure::CompactEffectHandle*);
    if (!reinterpret_cast<Valid>(g_mainBase + g_sites->handleValid)(&handle)) return {};
    const auto bases = read<const std::uintptr_t*>(reinterpret_cast<const void*>(g_mainBase), g_sites->poolBases);
    const auto strides = read<const std::uintptr_t*>(reinterpret_cast<const void*>(g_mainBase), g_sites->poolStrides);
    if (!bases || !strides) return {};
    const auto address = bases[handle.type] + strides[handle.type] * static_cast<unsigned>(handle.poolIndex);
    if (!address || read<std::uint32_t>(reinterpret_cast<const void*>(address), 0x20) != handle.eventId) return {};
    return {handle, address};
}

void tryBootstrap() {
    if (!g_bootstrapEnabled || !pose_session::active()) return;
    const auto session = g_owners.session();
    if (!session) return;
    using FindExecutor = const void* (*)(const void*);
    const auto findExecutor = reinterpret_cast<FindExecutor>(g_mainBase + g_findExecutor);
    for (unsigned slot = 0; slot < 2; ++slot) {
        if (g_bootstrapDone[slot] || g_bootstrapHandles[slot].poolIndex < 0 ||
            g_bootstrapTries[slot] >= 120) continue;
        ++g_bootstrapTries[slot];
        unsigned reason = 0;
        const auto binding = resolve(g_bootstrapHandles[slot]);
        if (!binding.event ||
            binding.event != g_expectedEvents[slot].load(std::memory_order_relaxed)) reason = 1;
        const void* executor = reason ? nullptr :
            findExecutor(reinterpret_cast<const void*>(binding.event));
        if (!reason && !executor) reason = 2;
        const void* set = executor ? read<const void*>(executor, 0xB8) : nullptr;
        const auto instance = executor ? read<std::uint32_t>(executor, 0xC0) : 0;
        if (!reason && (!set || read<std::uint32_t>(set, 0x234) != instance)) reason = 3;
        const auto* holder = reason ? nullptr :
            read<const void*>(reinterpret_cast<const void*>(g_mainBase), g_sites->elinkSystem);
        const auto* system = holder ? read<const void*>(holder, 0) : nullptr;
        if (!reason && !system) reason = 4;
        pure::RenderWristFrame wrist;
        if (!reason && !pose_render::copyWrist(session.historyGeneration, wrist)) reason = 5;
        float effect[12]{};
        if (!reason) {
            float columns[16], origin[3];
            std::memcpy(columns, static_cast<const std::byte*>(set) + 0x100, sizeof(columns));
            std::memcpy(origin, static_cast<const std::byte*>(system) + 0x64E74, sizeof(origin));
            if (!pure::effectRowsFromEmitterColumns(columns, origin, effect)) reason = 6;
        }
        const auto* anchor = g_bootstrapHaveInitialWrist ? g_bootstrapInitialWrist : wrist.matrix;
        if (!reason && bindEmitter(executor, session, anchor, effect)) g_bootstrapDone[slot] = true;
    }
}

using NativeVector = float __attribute__((vector_size(16)));
HOOK_DEFINE_TRAMPOLINE(CalculateEffectMatrixHook) {
    static NativeVector Callback(void* executor, void* output, void* resource, void* user,
                                 void* argument4, const void* descriptor, unsigned flags) {
        const auto event = read<const void*>(executor, 0x18);
        const auto eventId = event ? read<std::uint32_t>(event, 0x20) : 0;
        const auto owner = event ? g_owners.match(reinterpret_cast<std::uintptr_t>(event), eventId)
                                 : pure::WristEffectOwner{};
        if (!owner) {
            float matrix[12];
            const void* anchorUnit = nullptr;
            unsigned anchorBone = 0;
            const auto source = equipment_effects::copyMatrix(executor, descriptor, matrix,
                                                              &anchorUnit, &anchorBone);
            if (source != equipment_effects::EffectMatrix::Bone &&
                source != equipment_effects::EffectMatrix::ModelRoot)
                return Orig(executor, output, resource, user, argument4, descriptor, flags);
            pure::WristEffectOwners scope;
            const auto serial = scope.publish(1, {});
            pure::WristMatrixProvider provider(scope, serial, matrix);
            const auto historical = provider.descriptor();
            const auto result = Orig(executor, output, resource, user, argument4, &historical, flags);
            // Switch gear effects are re-anchored when particles consume them, like the wrist cue.
            if (!pure::kHistoricalEquipment && provider.copied() && anchorUnit) {
                if (const auto session = g_owners.session())
                    bindEmitter(executor, session, matrix, static_cast<const float*>(output), anchorUnit, anchorBone);
                else
                    emitterRefused(6);
            }
            return result;
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
        if (provider.copied()) bindEmitter(executor, owner, frame.matrix, static_cast<const float*>(output));
        return result;
    }
};
}

void install(std::uintptr_t mainBase) {
    g_sites = presentation_sites::valid(mainBase) ? presentation_sites::active() : nullptr;
    g_mainBase = g_sites ? mainBase : 0;
    if (!g_sites) return;
    const auto textSize = exl::util::GetMainModuleInfo().m_Text.m_Size;
    g_findExecutor = profiles::row(kFindExecutor);
    g_bootstrapEnabled = profiles::holds(g_mainBase, textSize, g_findExecutor, kFindExecutorWord) &&
                         profiles::holds(g_mainBase, textSize, g_sites->setEmitter, kSetEmitterWord);
    if (g_findExecutor && !g_bootstrapEnabled)
        Logging.Log("[self-recall] WRIST_BOOTSTRAP_UNAVAILABLE native executor lookup differs");
    CalculateEffectMatrixHook::InstallAtOffset(g_sites->calcMatrix);
    UpdateEmitterMatrixHook::InstallAtOffset(g_sites->updateEmitter);
}

void begin(std::uint32_t historyGeneration, std::span<const pure::CompactEffectHandle> handles) {
    g_owners.clear();
    g_emitters.clear();
    g_haveEmitterBinding.store(false, std::memory_order_relaxed);
    g_emitterRefusals.store(0, std::memory_order_relaxed);
    g_missingFrames.store(0, std::memory_order_relaxed);
    g_bootstrapHandles = {};
    g_bootstrapTries = {};
    g_bootstrapDone = {};
    g_bootstrapHaveInitialWrist = false;
    if (handles.size() > 2) {
        Logging.Log("[self-recall] WRIST_BIND_REFUSED handles=%u", static_cast<unsigned>(handles.size()));
        return;
    }
    pure::WristEffectBinding bindings[2]{};
    for (std::size_t i = 0; i < handles.size(); ++i) bindings[i] = resolve(handles[i]);
    for (unsigned i = 0; i < 2; ++i)
        g_expectedEvents[i].store(i < handles.size() ? bindings[i].event : 0, std::memory_order_relaxed);
    g_owners.publish(historyGeneration, {bindings, handles.size()});
    for (unsigned i = 0; i < handles.size(); ++i) g_bootstrapHandles[i] = handles[i];
    pure::RenderWristFrame initialWrist;
    if (pose_render::copyWrist(historyGeneration, initialWrist)) {
        std::memcpy(g_bootstrapInitialWrist, initialWrist.matrix, sizeof(g_bootstrapInitialWrist));
        g_bootstrapHaveInitialWrist = true;
    }
    tryBootstrap();
}

void serviceBootstrap() { tryBootstrap(); }

void end() {
    g_owners.clear();
    g_bootstrapHandles = {};
    g_bootstrapTries = {};
    g_bootstrapDone = {};
    g_bootstrapHaveInitialWrist = false;
    for (auto& event : g_expectedEvents) event.store(0, std::memory_order_relaxed);
    g_emitters.clear();
    g_haveEmitterBinding.store(false, std::memory_order_relaxed);
    g_emitterRefusals.store(0, std::memory_order_relaxed);
    g_missingFrames.store(0, std::memory_order_relaxed);
}

}
