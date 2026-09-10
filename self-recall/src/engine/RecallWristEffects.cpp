#include "RecallWristEffects.hpp"

#include <lib.hpp>
#include "RecallPoseRender.hpp"
#include "RecallEquipmentEffects.hpp"
#include "RecallPoseSession.hpp"
#include "RecallWristEmitter.hpp"

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
std::atomic<std::uint64_t> g_matches{0};
std::atomic<std::uint64_t> g_supplied{0};
std::atomic<std::uint64_t> g_missingFrames{0};
pure::WristEmitterFrames<> g_emitters;
std::atomic<std::uint64_t> g_emitterBindings{0}, g_emitterRefreshes{0}, g_emitterRefusals{0};

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
    g_emitterBindings.fetch_add(1, std::memory_order_relaxed);
}

void refreshEmitter(void* emitter) {
    if (!pose_session::active() || !g_emitterBindings.load(std::memory_order_relaxed)) return;
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
    const float dx = matrix[12] - read<float>(set, 0x130);
    const float dy = matrix[13] - read<float>(set, 0x134);
    const float dz = matrix[14] - read<float>(set, 0x138);
    using SetMatrix = void (*)(void*, const float*);
    reinterpret_cast<SetMatrix>(g_mainBase + kSetEmitterMatrix)(set, matrix);
    const float correction = std::sqrt(dx*dx + dy*dy + dz*dz);
    const auto count = g_emitterRefreshes.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 3 || count % 1800 == 0)
        Logging.Log("[self-recall] WRIST_EMITTER_REFRESH count=%llu key=%llu correction_cm=%d",
            static_cast<unsigned long long>(count), static_cast<unsigned long long>(wrist.key.serial),
            std::isfinite(correction) && correction < 100000 ? int(correction * 100) : -1);
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
        g_matches.fetch_add(1, std::memory_order_relaxed);
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
            const auto count = g_supplied.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count == 1 || count % 1800 == 0)
                Logging.Log("[self-recall] WRIST_FRAME count=%llu owner=%llu epoch=%llu key=%u:%llu",
                    static_cast<unsigned long long>(count), static_cast<unsigned long long>(owner.serial),
                    static_cast<unsigned long long>(frame.epoch), frame.key.generation,
                    static_cast<unsigned long long>(frame.key.serial));
        }
        return result;
    }
};
} // namespace

void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    CalculateEffectMatrixHook::InstallAtOffset(kCalcMatrix);
    UpdateEmitterMatrixHook::InstallAtOffset(kUpdateEmitterMatrix);
}

void begin(std::uint32_t historyGeneration, std::span<const pure::CompactEffectHandle> handles) {
    g_owners.clear();
    g_emitters.clear();
    g_emitterBindings.store(0, std::memory_order_relaxed);
    g_emitterRefreshes.store(0, std::memory_order_relaxed);
    g_emitterRefusals.store(0, std::memory_order_relaxed);
    g_matches.store(0, std::memory_order_relaxed);
    g_supplied.store(0, std::memory_order_relaxed);
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
    Logging.Log("[self-recall] WRIST_EMITTER_SUMMARY bindings=%llu refreshes=%llu refusals=%llu",
        static_cast<unsigned long long>(g_emitterBindings.exchange(0, std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_emitterRefreshes.exchange(0, std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_emitterRefusals.exchange(0, std::memory_order_relaxed)));
    const auto matched = g_matches.exchange(0, std::memory_order_relaxed);
    const auto supplied = g_supplied.exchange(0, std::memory_order_relaxed);
    const auto missing = g_missingFrames.exchange(0, std::memory_order_relaxed);
    if (matched || supplied || missing)
        Logging.Log("[self-recall] WRIST_SUMMARY matches=%llu supplied=%llu missing_frame=%llu",
            static_cast<unsigned long long>(matched), static_cast<unsigned long long>(supplied),
            static_cast<unsigned long long>(missing));
}

} // namespace self_recall::wrist_effects
