#include "RecallOffsets121.hpp"
#include "RecallScenePalette.hpp"
#include "RecallMonochromeFilter.hpp"

#include <atomic>
#include <bit>
#include <heap/seadHeap.h>
#include <lib.hpp>

#include "RecallGpuLifetimeBridge.hpp"
#include "RecallNativeGpuBuffer.hpp"
#include "RecallNativeSceneMaterial.hpp"
#include "RecallNativePath.hpp"
#include "RecallPaletteWrites.hpp"
#include "RecallPoseRender.hpp"
#include "RecallPoseStorage.hpp"

namespace self_recall::palette {
using namespace offsets121::palette;
namespace {
using BindUniform = void (*)(void*, unsigned, int, std::uint64_t, std::size_t);
using MapUniform = const std::byte* (*)(const void*);

std::uintptr_t g_mainBase = 0;
constinit NativeGpuBuffer g_gpu;
alignas(kGpuPoolAlignment) std::byte g_gpuBacking[kPaletteBackingBytes]{};
constinit pure::PaletteWrites g_writes;
pure::PaletteWriterIdentity g_seenIdentity{};
int g_seenCharacter = -1, g_seenOther = -1;
pure::PalettePreparedBuffer g_prepared{};
bool g_restorePending = false;
BindUniform g_bindUniform = nullptr;
MapUniform g_mapUniform = nullptr;
GpuBufferApi g_api{};
std::atomic<std::uintptr_t> g_mainSceneModel{0};
std::atomic<bool> g_ownerReady{false};
std::atomic<bool> g_frameActive{false};
std::atomic<unsigned> g_activeDraws{0};
bool g_allocationAttempted = false; // Sole native frame-start producer.
std::uint64_t g_epoch = 0, g_publication = 0;
std::uint32_t g_generation = 0;
std::atomic<std::uint64_t> g_failure{0}, g_uploads{0}, g_bindings{0}, g_restores{0};
std::atomic<std::uint64_t> g_nativeRestores{0};

constexpr Saturation kNativeMuted{0.375f, 0.25f};

enum class Failure : unsigned { Allocation = 1, Writer, Layout, Upload, Slots,
                               HistoricalModel, MissingPalette, DrawScope, Binding, Lifetime };
void fail(Failure reason, unsigned detail = 0) {
    std::uint64_t expected = 0;
    g_failure.compare_exchange_strong(expected, (std::uint64_t(reason) << 32) | detail,
                                     std::memory_order_release, std::memory_order_relaxed);
}
template<class T> T read(const void* object, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
std::uintptr_t address(const void* object) { return reinterpret_cast<std::uintptr_t>(object); }

pure::PaletteWriterIdentity identify(const void* model) {
    if (!model || read<std::uintptr_t>(model, 0) != g_mainBase + kModelVtable ||
        !read<std::uint16_t>(model, 0x16A)) return {};
    const auto* material = read<const void*>(model, 0x180);
    return material ? pure::PaletteWriterIdentity{address(model), address(material),
                                                   read<std::uintptr_t>(material, 0)}
                    : pure::PaletteWriterIdentity{};
}

struct Publication {
    pure::PaletteWriterIdentity identity{};
    std::uint64_t epoch = 0, serial = 0, gpu = 0, nativeGpu = 0;
    std::uintptr_t nativeBuffer = 0;
    std::uint32_t bytes = 0;
    unsigned nativeIndex = 0;
};
Publication g_slots[3]{};

struct DrawScope {
    std::uintptr_t argument = 0, unit = 0, commandBuffer = 0;
    std::uint64_t privateGpu = 0, nativeGpu = 0;
    std::uint32_t bytes = 0, locations = 0;
    bool substituted = false;
};
DrawScope g_draws[8]{};

DrawScope* findDraw(std::uintptr_t argument, std::uintptr_t unit) {
    for (auto& draw : g_draws)
        if (draw.argument == argument && draw.unit == unit && argument) return &draw;
    return nullptr;
}
bool usableLocations(std::uint32_t locations) {
    bool used = false;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const auto location = (locations >> shift) & 0xFFu;
        if (location == 0xFF) continue;
        if (location > 0x7F) return false;
        used = true;
    }
    return used;
}

void completeNativeWrites(const void* model, std::uint64_t epoch);

HOOK_DEFINE_TRAMPOLINE(MainSceneInitializeHook) {
    static u64 Callback(void* extension, void* heap, void* scene) {
        const auto result = Orig(extension, heap, scene);
        const auto* model = read<const void*>(extension, 0x8E8);
        g_mainSceneModel.store(address(model), std::memory_order_release);
        monochrome::initializeForScene(extension);
        native_path::initializeForScene(extension, scene, static_cast<sead::Heap*>(heap));
        return result;
    }
};

using NativeSaturationWrite = u64 (*)(void*, int, int, float);
u64 writeSceneSaturation(void* model, int material, int parameter, float value,
                         std::uintptr_t caller, NativeSaturationWrite writeNative) {
    const bool character = caller == g_mainBase + kCharacterSaturationReturn;
    if (!character && caller != g_mainBase + kWorldSaturationReturn)
        return writeNative(model, material, parameter, value);
    if (address(model) != g_mainSceneModel.load(std::memory_order_acquire))
        return writeNative(model, material, parameter, value);
    std::uint64_t epoch, result;
    bool complete = false;
    {
        gpu_lifetime::Access access;
        epoch = g_epoch;
        const auto identity = identify(model);
        const bool muted = g_writes.recall(epoch);
        const float applied = muted ? (character ? kNativeMuted.character : kNativeMuted.other) : value;
        if (muted) g_restorePending = true;
        result = writeNative(model, material, parameter, applied);
        if (identity != g_seenIdentity) {
            g_seenIdentity = identity;
            g_seenCharacter = g_seenOther = -1;
        }
        if (material == 0) (character ? g_seenCharacter : g_seenOther) = parameter;
        if (!g_writes.record(epoch, identity, material,
                character ? pure::PaletteParameter::Character : pure::PaletteParameter::Other,
                parameter, applied) && muted) fail(Failure::Writer);
        complete = !character && g_writes.complete(epoch, identity) && (muted || g_restorePending);
    }
    if (complete) completeNativeWrites(model, epoch);
    return result;
}

HOOK_DEFINE_TRAMPOLINE(SaturationWriteHook) {
    static u64 Callback(void* model, int material, int parameter, float value) {
        const auto caller = address(__builtin_return_address(0));
        return writeSceneSaturation(model, material, parameter, value, caller,
            [](void* target, int mat, int param, float saturation) {
                return Orig(target, mat, param, saturation);
            });
    }
};

HOOK_DEFINE_TRAMPOLINE(ForceDrawSetupHook) {
    static u64 Callback(void* model) {
        const auto result = Orig(model);
        afterNativeModel(model);
        monochrome::protectHistoricalModel(model);
        return result;
    }
};

HOOK_DEFINE_INLINE(ShaderGateHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_activeDraws.load(std::memory_order_relaxed)) return;
        gpu_lifetime::Access access;
        if (findDraw(ctx->X[19], ctx->X[22])) {
            ctx->X[25] = ~ctx->X[16];
        }
    }
};
HOOK_DEFINE_INLINE(SceneCompareHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_activeDraws.load(std::memory_order_relaxed)) return;
        gpu_lifetime::Access access;
        if (findDraw(ctx->X[19], ctx->X[22])) ctx->X[8] = ~std::uint32_t(ctx->X[26]);
    }
};
HOOK_DEFINE_INLINE(SceneAddressHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_activeDraws.load(std::memory_order_relaxed)) return;
        gpu_lifetime::Access access;
        auto* draw = findDraw(ctx->X[19], ctx->X[22]);
        if (!draw) return;
        const auto locations = std::uint32_t(ctx->X[26]);
        if (ctx->X[0] != draw->nativeGpu || ctx->X[20] != draw->bytes ||
            ctx->X[25] != draw->commandBuffer) {
            fail(Failure::Binding);
            return;
        }
        if (!usableLocations(locations)) return;
        draw->locations = locations;
        draw->substituted = true;
        ctx->X[0] = draw->privateGpu;
    }
};

int beginDraw(const void* unit, void* argument) {
    if (!g_frameActive.load(std::memory_order_acquire) || !unit || !argument) return -1;
    if (read<std::uint8_t>(argument, 0x43) & 2u) return -1;
    std::uint64_t epoch;
    std::uint32_t generation;
    {
        gpu_lifetime::Access access;
        if (!g_writes.recall(g_epoch)) return -1;
        epoch = g_epoch;
        generation = g_generation;
    }
    const auto* model = read<const void*>(unit, 8);
    unsigned failureDetail = 0;
    const auto owned = pose_render::paletteModel(model, epoch, generation, &failureDetail);
    if (owned == pose_render::PaletteModel::Foreign) return -1;
    if (owned != pose_render::PaletteModel::Ready) { fail(Failure::HistoricalModel, failureDetail); return -1; }
    const auto* context = read<const void*>(argument, 0x10);
    const auto* drawContext = read<const void*>(argument, 0x20);
    if (!context || !drawContext) { fail(Failure::Binding, 1); return -1; }
    const auto* sceneModel = read<const void*>(context, 0x590);
    const auto identity = identify(sceneModel);
    const auto cb = read<std::uintptr_t>(drawContext, 0xB8);
    if (!identity || !cb) { fail(Failure::Binding, 2); return -1; }

    gpu_lifetime::Access access;
    if (epoch != g_epoch || generation != g_generation) { fail(Failure::Binding, 3); return -1; }
    const Publication* selected = nullptr;
    unsigned slot = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const auto& candidate = g_slots[i];
        if (candidate.epoch == epoch && candidate.identity == identity && candidate.gpu &&
            candidate.nativeIndex == (read<std::uint8_t>(sceneModel, 0x15) & 3u) &&
            (!selected || candidate.serial > selected->serial)) { selected = &candidate; slot = i; }
    }
    if (!selected) { fail(Failure::MissingPalette); return -1; }
    for (unsigned i = 0; i < 8; ++i) if (!g_draws[i].argument) {
        if (!access.ledger().bindSlot(cb, slot)) {
            fail(Failure::Lifetime, access.noteFailure(gpu_lifetime::Operation::PrivateBind));
            access.logBindingFailure(cb, slot);
            return -1;
        }
        g_draws[i] = {address(argument), address(unit), cb, selected->gpu,
                      selected->nativeGpu, selected->bytes, 0, false};
        g_activeDraws.fetch_add(1, std::memory_order_relaxed);
        return static_cast<int>(i);
    }
    fail(Failure::DrawScope);
    return -1;
}

void endDraw(int index) {
    if (index < 0) return;
    DrawScope completed;
    {
        gpu_lifetime::Access access;
        completed = g_draws[index];
        g_draws[index] = {};
        g_activeDraws.fetch_sub(1, std::memory_order_relaxed);
    }
    if (!completed.substituted) return;
    constexpr unsigned stages[]{0, 1, 2, 5};
    for (unsigned i = 0; i < 4; ++i) {
        const auto location = (completed.locations >> (8 * i)) & 0xFFu;
        if (location != 0xFF)
            g_bindUniform(reinterpret_cast<void*>(completed.commandBuffer), stages[i],
                          static_cast<int>(location), completed.nativeGpu, completed.bytes);
    }
    g_bindings.fetch_add(1, std::memory_order_relaxed);
    g_restores.fetch_add(1, std::memory_order_relaxed);
}
HOOK_DEFINE_TRAMPOLINE(ModelDrawHook) {
    static u64 Callback(const void* unit, void* argument, unsigned pass, unsigned flags) {
        if (!pose_render::drawVisible(unit)) return 0;
        const auto scope = beginDraw(unit, argument);
        const auto result = Orig(unit, argument, pass, flags);
        endDraw(scope);
        return result;
    }
};
} // namespace

void beginFrame(std::uint64_t epoch, std::uint32_t animationGeneration) {
    if (!g_mainBase) return;
    if (!g_allocationAttempted && pose_storage::history()) {
        g_api = resolveGpuBufferApi(g_mainBase);
        const auto bindSlot = read<std::uintptr_t>(reinterpret_cast<void*>(g_mainBase), kBindUniformSlot);
        g_bindUniform = bindSlot ? read<BindUniform>(reinterpret_cast<void*>(bindSlot), 0) : nullptr;
        if (g_api && g_bindUniform) {
            g_allocationAttempted = true;
            const auto status = g_gpu.create(g_api, g_gpuBacking, UINT16_MAX, 3);
            if (status == GpuBufferStatus::Ready) {
                g_ownerReady.store(true, std::memory_order_release);
                gpu_lifetime::requestTracking();
            } else fail(Failure::Allocation, static_cast<unsigned>(status));
            Logging.Log("[self-recall] PALETTE_STORAGE owner=module_bss status=%u bytes=%u flags=%u",
                        static_cast<unsigned>(status), g_gpu.layout().total, g_api.memoryPoolFlags);
        }
    }
    if (g_generation != animationGeneration && g_generation)
        Logging.Log("[self-recall] PALETTE_SUMMARY generation=%u uploads=%llu binds=%llu restores=%llu native_restores=%llu",
            g_generation, static_cast<unsigned long long>(g_uploads.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_bindings.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_restores.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_nativeRestores.load(std::memory_order_relaxed)));
    gpu_lifetime::Access access;
    g_epoch = epoch;
    g_generation = animationGeneration;
    g_prepared = {};
    if (animationGeneration && (!g_ownerReady.load(std::memory_order_acquire) ||
        !gpu_lifetime::bindingPhaseReady())) fail(Failure::Allocation, 0x100);
    if (animationGeneration && access.ledger().failed())
        fail(Failure::Lifetime, access.failureOperation());
    g_writes.beginFrame(epoch, animationGeneration && g_ownerReady.load(std::memory_order_acquire) &&
                               gpu_lifetime::bindingPhaseReady() && !access.ledger().failed());
    g_frameActive.store(g_writes.recall(epoch), std::memory_order_release);
    monochrome::beginFrame(epoch, g_writes.recall(epoch) ? animationGeneration : 0);
}

void afterNativeModel(const void* model) {
    if (!g_ownerReady.load(std::memory_order_acquire) ||
        address(model) != g_mainSceneModel.load(std::memory_order_acquire)) return;
    gpu_lifetime::Access access;
    if (!model || address(model) != g_seenIdentity.model) return;
    const auto identity = identify(model);
    if (identity != g_seenIdentity) return;
    SceneMaterialView view;
    if (describeSceneMaterial(model, g_mainBase, 0, g_seenCharacter, g_seenOther, view) != MaterialStatus::Ready)
        return;
    const auto* mapped = g_mapUniform(view.nativeBuffer);
    const std::span<const std::byte> bytes(mapped, mapped ? view.uniformBytes : 0);
    const Saturation source{detail::read<float>(view.source, view.character.sourceOffset),
                            detail::read<float>(view.source, view.other.sourceOffset)};
    if (verifyScenePaletteUpload(view, bytes, source) == MaterialStatus::Ready)
        g_prepared = {identity, g_epoch, address(view.nativeBuffer), view.bufferIndex, view.uniformBytes};
}

namespace {
void completeNativeWrites(const void* model, std::uint64_t epoch) {
    SceneMaterialView view;
    pure::PaletteWriterIdentity identity;
    Saturation expected{};
    bool muted;
    {
        gpu_lifetime::Access access;
        identity = identify(model);
        if (!g_writes.complete(epoch, identity)) { fail(Failure::Writer, 1); return; }
        const auto described = describeSceneMaterial(model, g_mainBase, 0,
            g_writes.index(pure::PaletteParameter::Character), g_writes.index(pure::PaletteParameter::Other), view);
        if (described != MaterialStatus::Ready) { fail(Failure::Layout, unsigned(described)); return; }
        if (!g_prepared.matches(identity, epoch, address(view.nativeBuffer), view.bufferIndex, view.uniformBytes)) {
            fail(Failure::Upload, 0x400); return;
        }
        expected = {g_writes.value(pure::PaletteParameter::Character),
                    g_writes.value(pure::PaletteParameter::Other)};
        muted = g_writes.recall(epoch);
    }
    using CalculateMaterial = u64 (*)(const void*, unsigned);
    reinterpret_cast<CalculateMaterial>(g_mainBase + kCalculateMaterial)(view.material, view.bufferIndex);
    const auto* mapped = g_mapUniform(view.nativeBuffer);
    const std::span<const std::byte> bytes(mapped, mapped ? view.uniformBytes : 0);
    const auto verified = verifyScenePaletteUpload(view, bytes, expected);
    if (verified != MaterialStatus::Ready) { fail(Failure::Upload, unsigned(verified)); return; }
    const auto nativeGpu = g_api.address(read<const void*>(view.nativeBuffer, 8));
    if (!nativeGpu || (nativeGpu & 0xFFu) ||
        nativeGpu > UINT64_MAX - ((view.uniformBytes + 0xFFu) & ~0xFFu)) {
        fail(Failure::Upload, 0x100); return;
    }
    std::uint64_t count = 0;
    unsigned usedSlot = 3;
    {
        gpu_lifetime::Access access;
        if (epoch != g_epoch || !g_writes.complete(epoch, identity) || g_publication == UINT64_MAX) {
            fail(Failure::Writer, 2); return;
        }
        g_restorePending = muted;
        if (!muted) {
            count = g_nativeRestores.fetch_add(1, std::memory_order_relaxed) + 1;
        }
        for (unsigned i = 0; muted && i < 3; ++i) if (access.ledger().canWrite(i)) {
            g_gpu.retireAfterGpuFence(i);
            const std::array<GpuWordPatch, 2> patches{{
                {view.character.uniformOffset, std::bit_cast<std::uint32_t>(1.0f)},
                {view.other.uniformOffset, std::bit_cast<std::uint32_t>(1.0f)}}};
            const auto uploaded = g_gpu.uploadPatched(i, bytes, patches);
            if (uploaded != GpuBufferStatus::Ready) { fail(Failure::Upload, 0x200u | unsigned(uploaded)); return; }
            const auto gpu = g_gpu.addressForSubmission(i);
            if (!gpu) { fail(Failure::Upload, 0x300); return; }
            g_slots[i] = {identity, g_epoch, ++g_publication, gpu, nativeGpu,
                          address(view.nativeBuffer), view.uniformBytes, view.bufferIndex};
            count = g_uploads.fetch_add(1, std::memory_order_relaxed) + 1;
            usedSlot = i;
            break;
        }
        if (muted && usedSlot == 3) { fail(Failure::Slots); return; }
    }
    if (!muted) {
        Logging.Log("[self-recall] PALETTE_NATIVE_RESTORE count=%llu epoch=%llu chara_milli=%d other_milli=%d",
            static_cast<unsigned long long>(count), static_cast<unsigned long long>(epoch),
            static_cast<int>(expected.character * 1000.0f), static_cast<int>(expected.other * 1000.0f));
        return;
    }
    if (count == 1 || count % 1800 == 0)
        Logging.Log("[self-recall] PALETTE_UPLOAD count=%llu epoch=%llu bytes=%u slot=%u binds=%llu restores=%llu world_milli=375,250 link_milli=1000",
            static_cast<unsigned long long>(count), static_cast<unsigned long long>(epoch),
            view.uniformBytes, usedSlot, static_cast<unsigned long long>(g_bindings.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_restores.load(std::memory_order_relaxed)));
}
} // namespace

std::uint64_t takeFailure() {
    if (const auto failure = g_failure.exchange(0, std::memory_order_acq_rel)) return failure;
    const auto filter = monochrome::takeFailure();
    return filter ? (std::uint64_t{11} << 32) | ((filter >> 32) << 24) | (filter & 0xFFFFFFu) : 0;
}
const char* failureName(std::uint64_t failure) {
    if ((failure >> 32) == 11) return "screen-filter";
    switch (static_cast<Failure>(failure >> 32)) {
    case Failure::Allocation: return "allocation";
    case Failure::Writer: return "writer";
    case Failure::Layout: return "layout";
    case Failure::Upload: return "upload";
    case Failure::Slots: return "slots";
    case Failure::HistoricalModel: return "historical-model";
    case Failure::MissingPalette: return "missing-palette";
    case Failure::DrawScope: return "draw-scope";
    case Failure::Binding: return "binding";
    case Failure::Lifetime: return "lifetime";
    }
    return "unknown";
}
void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    monochrome::install(mainBase);
    g_mapUniform = reinterpret_cast<MapUniform>(mainBase + kMapUniform);
    gpu_lifetime::install(mainBase);
    MainSceneInitializeHook::InstallAtOffset(kInitializeMainScene);
    SaturationWriteHook::InstallAtOffset(kWriteSaturation);
    ForceDrawSetupHook::InstallAtOffset(kPrepareShapeDraw);
    ModelDrawHook::InstallAtOffset(kDrawModelShape);
    ShaderGateHook::InstallAtOffset(kShaderGate);
    SceneCompareHook::InstallAtOffset(kCompareSceneBinding);
    SceneAddressHook::InstallAtOffset(kSceneBindingAddress);
}
} // namespace self_recall::palette
