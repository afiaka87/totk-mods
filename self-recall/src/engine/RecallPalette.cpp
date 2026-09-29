#include "totk/engine/ReadGuard.hpp"
#include "RecallRuntimeEngine.hpp"
#include "RecallGraphicsEngine.hpp"
#include "RecallEffectsEngine.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <heap/seadHeap.h>
#include <lib.hpp>

#include "RecallVisual.hpp"
#include "RecallModelEngine.hpp"
#include "GameProfiles.hpp"

namespace self_recall::palette {
struct PaletteSites {
    std::uintptr_t drawFilter, findParameter, calculateMaterial, mapUniform;
    std::uintptr_t initializeScene, writeSaturation, prepareShapeDraw, drawModelShape;
    std::uintptr_t primitiveTexturesSlot, bindUniformSlot;
    std::uintptr_t characterReturn, worldReturn;
};

struct PaletteBinding {
    std::uintptr_t shaderGate, sceneCompare, sceneAddress;
    std::uint8_t shaderLeft, shaderRight, argument, unit, sceneKey;
    std::uint8_t gpu, commandBuffer, bytes, locations;
    bool helper;
};

struct PaletteProfile {
    SceneModelLayout model;
    PaletteSites sites;
    PaletteBinding binding;
    std::size_t sceneModelContext;
    bool dynamicSaturation;
    bool sixArgumentFilter;
};

const PaletteProfile& profile() {
    static constexpr std::array<PaletteProfile, 9> builds{{
        {{0x44EA7C0, 0x16A, 0x180, 0x15},
         {0xBE1BA0, 0x29CE25C, 0x73E140, 0x9EEADC, 0xC39D3C, 0x29CE50C,
          0x21A3924, 0x7046C4, 0x455E280, 0x4540968, 0, 0},
         {}, 0x590, false, true},
        {{0x45C2E50, 0x16A, 0x180, 0x15},
         {0x95E42C, 0x2A465A4, 0x761B08, 0x9C2C38, 0xCA6304, 0x2A46854,
          0x2223634, 0x68EC90, 0x46373C0, 0x4619970, 0x968280, 0x9682E0},
         {0x68ED68, 0x68FD50, 0x9ADF64, 25, 24, 19, 22, 25, 0, 21, 26, 24, true},
         0x590, true, false},
        {{0x45BD1B8, 0x16A, 0x180, 0x15},
         {0x92FF3C, 0x2A3D70C, 0x7B3A30, 0x997F2C, 0xC6FFAC, 0x2A3D9BC,
          0x22184F0, 0x688C68, 0x4631740, 0x4613CE0, 0x936C44, 0x936CA4},
         {0x688D40, 0x689DA4, 0x689E30, 25, 6, 19, 22, 28, 0, 26, 20, 28, false},
         0x590, true, false},
        {{0x45B1570, 0x16A, 0x180, 0x15},
         {0xB62300, 0x2A31574, 0x748338, 0x9C1E38, 0xC3B708, 0x2A31824,
          0x220EA34, 0x6480F0, 0x4625AE8, 0x4608098, 0xAE3EC0, 0xAE3F20},
         {0x6481C8, 0x6491DC, 0x99F314, 25, 24, 19, 22, 25, 0, 21, 23, 24, true},
         0x590, true, false},
        {{0x45C0570, 0x16A, 0x180, 0x15},
         {0xC31DDC, 0x2A408C4, 0x76B974, 0x93F32C, 0xC77B04, 0x2A40B74,
          0x2218F9C, 0x74C284, 0x4634AE0, 0x4617088, 0x9AB480, 0x9AB4E0},
         {0x74C360, 0x74D500, 0x74D584, 25, 16, 19, 22, 26, 0, 25, 20, 26, false},
         0x590, true, false},
        {{0x38FB0F8, 0x10A, 0x120, 0x14},
         {0x54107C, 0x4EC760, 0xA15440, 0x17651C, 0xB54288, 0x4EC6C4,
          0x283E92C, 0x833490, 0x3956330, 0x395F308, 0x4EBF6C, 0x4EBFCC},
         {0x17E988, 0x17F608, 0x17F6C4, 26, 23, 19, 20, 14, 3, 0, 26, 27, false},
         0x588, true, false},
        {{0x38F60F8, 0x10A, 0x120, 0x14},
         {0x39F81C, 0x5B7FF0, 0xA0CDA8, 0x1601F8, 0xB4F5D0, 0x5B7F58,
          0x2836BF4, 0x836490, 0x3951318, 0x395A290, 0x5B7B84, 0x5B7BE4},
         {0xBE720, 0xBF37C, 0xBF428, 26, 23, 19, 20, 22, 3, 0, 27, 22, false},
         0x588, true, false},
        {{0x38F80F8, 0x10A, 0x120, 0x14},
         {0x2DB318, 0x493B60, 0xA81174, 0xD2EC8, 0xB38C84, 0x493AC4,
          0x2836EB4, 0x86B910, 0x3953340, 0x395C2F0, 0x4932FC, 0x49335C},
         {0xDBC48, 0xDC508, 0xDC71C, 6, 24, 19, 26, 10, 21, 23, 22, 28, false},
         0x588, true, false},
        {{0x390A0F8, 0x10A, 0x120, 0x14},
         {0x7AA268, 0x77A390, 0xA36118, 0x8219C, 0xB70BD0, 0x77A2F8,
          0x2845334, 0x77E760, 0x3965360, 0x396E320, 0x89F94, 0x89FF4},
         {0x876D4, 0x88358, 0x88404, 26, 23, 19, 20, 22, 3, 0, 26, 22, false},
         0x588, true, false},
    }};
    return profiles::row(builds);
}

constexpr std::array<std::array<std::uint32_t, 8>, 9> kHookWords{{
    {0xD10403FF, 0xA9BE7BFD, 0xD10603FF, 0, 0, 0, 0, 0},
    {0xD10403FF, 0xA9BE7BFD, 0xD10643FF, 0x39405408, 0xD10343FF, 0xEB18033F, 0x6B08033F, 0xAA0003F3},
    {0xD10403FF, 0xA9BE7BFD, 0xD10643FF, 0x39405408, 0xD10383FF, 0xEB06033F, 0x6B08039F, 0xAA0003E3},
    {0xD10403FF, 0xA9BE7BFD, 0xD10643FF, 0x39405408, 0xD10343FF, 0xEB18033F, 0x6B08033F, 0xAA0003F3},
    {0xD10403FF, 0xA9BE7BFD, 0xD10643FF, 0x39405408, 0xD10383FF, 0xEB10033F, 0x6B08035F, 0xAA0003E3},
    {0xD103C3FF, 0xA9BE7BFD, 0xB9406408, 0x39405008, 0x39407008, 0xEB17035F, 0x6B0801DF, 0x9001BE90},
    {0xD103C3FF, 0xA9BE7BFD, 0xB9406408, 0x39405008, 0x39407008, 0xEB17035F, 0x6B0802DF, 0x2A1A03EC},
    {0xD103C3FF, 0xA9BE7BFD, 0xB9406408, 0x39405008, 0x39407008, 0xEB1800DF, 0x6B08015F, 0x1103FE89},
    {0xD10403FF, 0xA9BE7BFD, 0xB9406408, 0x39405008, 0x39407008, 0xEB17035F, 0x6B0802DF, 0xD001C6AF},
}};

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
bool g_allocationAttempted = false;
bool g_apiMissingReported = false;
std::uint64_t g_epoch = 0, g_publication = 0;
std::uint32_t g_generation = 0;
std::atomic<std::uint64_t> g_failure{0};


constexpr Saturation kNativeMuted{0.375f, 0.25f};

enum class Failure : unsigned { Allocation = 1, Writer, Layout, Upload, Slots,
                               HistoricalModel, MissingPalette, DrawScope, Binding, Lifetime };
void fail(Failure reason, unsigned detail = 0) {
    std::uint64_t expected = 0;
    g_failure.compare_exchange_strong(expected, (std::uint64_t(reason) << 32) | detail,
                                     std::memory_order_release, std::memory_order_relaxed);
}
template<class T> T read(const void* object, std::size_t offset) {
    if (!totk::engine::read_guard::admit(object, offset, sizeof(T))) return T{};
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
std::uintptr_t address(const void* object) { return reinterpret_cast<std::uintptr_t>(object); }

pure::PaletteWriterIdentity identify(const void* model) {
    const auto& layout = profile().model;
    if (!model || read<std::uintptr_t>(model, 0) != g_mainBase + layout.vtable ||
        !read<std::uint16_t>(model, layout.materialCount)) return {};
    const auto* material = read<const void*>(model, layout.materials);
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
DrawScope* findDrawByBinding(std::uintptr_t commandBuffer, std::uint64_t nativeGpu) {
    for (auto& draw : g_draws)
        if (draw.argument && draw.commandBuffer == commandBuffer && draw.nativeGpu == nativeGpu)
            return &draw;
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
    const auto& sites = profile().sites;
    const bool character = caller == g_mainBase + sites.characterReturn;
    if (!character && caller != g_mainBase + sites.worldReturn)
        return writeNative(model, material, parameter, value);
    if (address(model) != g_mainSceneModel.load(std::memory_order_acquire))
        return writeNative(model, material, parameter, value);
    if (character && profiles::newerRenderer()) afterNativeModel(model);
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
        afterNativeModel(model, true);
        monochrome::protectHistoricalModel(model);
        return result;
    }
};

HOOK_DEFINE_INLINE(ShaderGateHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_activeDraws.load(std::memory_order_relaxed)) return;
        gpu_lifetime::Access access;
        const auto& binding = profile().binding;
        if (findDraw(ctx->X[binding.argument], ctx->X[binding.unit]))
            ctx->X[binding.shaderLeft] = ~ctx->X[binding.shaderRight];
    }
};
HOOK_DEFINE_INLINE(SceneCompareHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_activeDraws.load(std::memory_order_relaxed)) return;
        gpu_lifetime::Access access;
        const auto& binding = profile().binding;
        if (findDraw(ctx->X[binding.argument], ctx->X[binding.unit]))
            ctx->X[8] = ~std::uint32_t(ctx->X[binding.sceneKey]);
    }
};
HOOK_DEFINE_INLINE(SceneAddressHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_activeDraws.load(std::memory_order_relaxed)) return;
        gpu_lifetime::Access access;
        const auto& binding = profile().binding;
        const auto gpu = ctx->X[binding.gpu];
        const auto commandBuffer = ctx->X[binding.commandBuffer];
        auto* draw = binding.helper ? findDrawByBinding(commandBuffer, gpu) :
            findDraw(ctx->X[binding.argument], ctx->X[binding.unit]);
        if (!draw) return;
        const auto locations = std::uint32_t(ctx->X[binding.locations]);
        if (gpu != draw->nativeGpu || ctx->X[binding.bytes] != draw->bytes ||
            commandBuffer != draw->commandBuffer) {
            fail(Failure::Binding);
            return;
        }
        if (!usableLocations(locations)) return;
        draw->locations = locations;
        draw->substituted = true;
        ctx->X[binding.gpu] = draw->privateGpu;
    }
};

int beginDraw(const void* unit, void* argument, unsigned pass, std::uintptr_t caller) {
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
    const auto* sceneModel = read<const void*>(context, profile().sceneModelContext);
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
            candidate.nativeIndex == (read<std::uint8_t>(sceneModel, profile().model.bufferIndex) & 3u) &&
            (!selected || candidate.serial > selected->serial)) { selected = &candidate; slot = i; }
    }
    if (!selected) { fail(Failure::MissingPalette); return -1; }
    if (profiles::newerRenderer() && !access.ledger().hasRecorder(cb) &&
        !access.ledger().hasRecordingFrame(cb)) {
        access.logUntrackedDraw(caller);
        return -1;
    }
    for (unsigned i = 0; i < 8; ++i) if (!g_draws[i].argument) {
        if (!access.ledger().bindSlot(cb, slot)) {
            fail(Failure::Lifetime, access.noteFailure(gpu_lifetime::Operation::PrivateBind));
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
}
HOOK_DEFINE_TRAMPOLINE(ModelDrawHook) {
    static u64 Callback(const void* unit, void* argument, unsigned pass, unsigned flags) {
        if (profiles::newerRenderer() && unit) {
            const auto nativePasses = (read<std::uint8_t>(unit, 0x1C) >> 4) & 7u;
            if (static_cast<std::int32_t>(pass) >= static_cast<std::int32_t>(nativePasses))
                return Orig(unit, argument, pass, flags);
        }
        if (!pose_render::drawVisible(unit)) return 0;
        const auto scope = beginDraw(unit, argument, pass, address(__builtin_return_address(0)));
        const auto result = Orig(unit, argument, pass, flags);
        endDraw(scope);
        return result;
    }
};
}

bool sitesValid(std::uintptr_t mainBase, std::size_t textSize) {
    const auto& p = profile();
    const std::array<std::uintptr_t, 8> offsets{p.sites.initializeScene,
        p.sites.prepareShapeDraw, p.sites.drawFilter, p.sites.writeSaturation,
        p.sites.drawModelShape, p.binding.shaderGate, p.binding.sceneCompare,
        p.binding.sceneAddress};
    if (!profiles::holds(mainBase, textSize, offsets, profiles::row(kHookWords),
                         p.dynamicSaturation ? offsets.size() : 3u))
        return false;
    return true;
}

void beginFrame(std::uint64_t epoch, std::uint32_t animationGeneration) {
    if (!g_mainBase) return;
    if (!profile().dynamicSaturation) {
        g_epoch = epoch;
        g_generation = animationGeneration;
        g_frameActive.store(false, std::memory_order_release);
        monochrome::beginFrame(epoch, animationGeneration);
        return;
    }
    if (!g_allocationAttempted && pose_storage::history()) {
        g_api = resolveGpuBufferApi(g_mainBase);
        const auto bindSlot = read<std::uintptr_t>(reinterpret_cast<void*>(g_mainBase),
                                                profile().sites.bindUniformSlot);
        g_bindUniform = bindSlot ? read<BindUniform>(reinterpret_cast<void*>(bindSlot), 0) : nullptr;
        if (g_api && g_bindUniform) {
            g_allocationAttempted = true;
            const auto status = g_gpu.create(g_api, g_gpuBacking, UINT16_MAX, 3);
            if (status == GpuBufferStatus::Ready) {
                g_ownerReady.store(true, std::memory_order_release);
                gpu_lifetime::requestTracking();
            } else fail(Failure::Allocation, static_cast<unsigned>(status));
        } else if (!g_apiMissingReported) {
            g_apiMissingReported = true;
            Logging.Log("[self-recall] PALETTE_API_MISSING api=%u bind=%u",
                        static_cast<unsigned>(bool(g_api)), static_cast<unsigned>(g_bindUniform != nullptr));
        }
    }
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

void afterNativeModel(const void* model, [[maybe_unused]] bool forced) {
    if (!profile().dynamicSaturation) return;
    if (!g_ownerReady.load(std::memory_order_acquire) ||
        address(model) != g_mainSceneModel.load(std::memory_order_acquire)) return;
    gpu_lifetime::Access access;
    if (!model || address(model) != g_seenIdentity.model) return;
    const auto identity = identify(model);
    if (identity != g_seenIdentity) return;
    SceneMaterialView view;
    const auto described = describeSceneMaterial(model, g_mainBase, 0,
        g_seenCharacter, g_seenOther, view, profile().model);
    if (described != MaterialStatus::Ready) return;
    const auto* mapped = g_mapUniform(view.nativeBuffer);
    const std::span<const std::byte> bytes(mapped, mapped ? view.uniformBytes : 0);
    const Saturation source{detail::read<float>(view.source, view.character.sourceOffset),
                            detail::read<float>(view.source, view.other.sourceOffset)};
    const auto verified = verifyScenePaletteUpload(view, bytes, source);
    if (verified == MaterialStatus::Ready)
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
            g_writes.index(pure::PaletteParameter::Character), g_writes.index(pure::PaletteParameter::Other),
            view, profile().model);
        if (described != MaterialStatus::Ready) { fail(Failure::Layout, unsigned(described)); return; }
        if (!g_prepared.matches(identity, epoch, address(view.nativeBuffer), view.bufferIndex, view.uniformBytes)) {
            fail(Failure::Upload, 0x400); return;
        }
        expected = {g_writes.value(pure::PaletteParameter::Character),
                    g_writes.value(pure::PaletteParameter::Other)};
        muted = g_writes.recall(epoch);
    }
    using CalculateMaterial = u64 (*)(const void*, unsigned);
    reinterpret_cast<CalculateMaterial>(g_mainBase + profile().sites.calculateMaterial)(
        view.material, view.bufferIndex);
    const auto* mapped = g_mapUniform(view.nativeBuffer);
    const std::span<const std::byte> bytes(mapped, mapped ? view.uniformBytes : 0);
    const auto verified = verifyScenePaletteUpload(view, bytes, expected);
    if (verified != MaterialStatus::Ready) { fail(Failure::Upload, unsigned(verified)); return; }
    const auto nativeGpu = g_api.address(read<const void*>(view.nativeBuffer, 8));
    if (!nativeGpu || (nativeGpu & 0xFFu) ||
        nativeGpu > UINT64_MAX - ((view.uniformBytes + 0xFFu) & ~0xFFu)) {
        fail(Failure::Upload, 0x100); return;
    }
    unsigned usedSlot = 3;
    {
        gpu_lifetime::Access access;
        if (epoch != g_epoch || !g_writes.complete(epoch, identity) || g_publication == UINT64_MAX) {
            fail(Failure::Writer, 2); return;
        }
        g_restorePending = muted;
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
            usedSlot = i;
            break;
        }
        if (muted && usedSlot == 3) { fail(Failure::Slots); return; }
    }
}
}

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
    const auto& sites = profile().sites;
    g_mapUniform = reinterpret_cast<MapUniform>(mainBase + sites.mapUniform);
    if (profile().dynamicSaturation) gpu_lifetime::install(mainBase);
    MainSceneInitializeHook::InstallAtOffset(sites.initializeScene);
    ForceDrawSetupHook::InstallAtOffset(sites.prepareShapeDraw);
    if (profile().dynamicSaturation) {
        SaturationWriteHook::InstallAtOffset(sites.writeSaturation);
        ModelDrawHook::InstallAtOffset(sites.drawModelShape);
        ShaderGateHook::InstallAtOffset(profile().binding.shaderGate);
        SceneCompareHook::InstallAtOffset(profile().binding.sceneCompare);
        SceneAddressHook::InstallAtOffset(profile().binding.sceneAddress);
    }
}
}

namespace self_recall::monochrome {
namespace {
std::uintptr_t g_mainBase = 0;
std::atomic<std::uintptr_t> g_filter{0};
std::atomic<std::uint64_t> g_epoch{0}, g_failure{0};
std::atomic<std::uint32_t> g_generation{0};

enum class Failure : unsigned { Filter = 1, Shader, ZeroTexture, Model, Material,
    Parameter, Attribute, Upload };
void fail(Failure reason, unsigned detail = 0) {
    std::uint64_t empty = 0;
    if (g_failure.compare_exchange_strong(empty, (std::uint64_t(reason) << 32) | detail))
        Logging.Log("[self-recall] MONOCHROME_FAILURE reason=%u detail=%u epoch=%llu",
            unsigned(reason), detail, static_cast<unsigned long long>(g_epoch.load()));
}
template<class T> T read(const void* object, std::size_t offset) {
    return palette::detail::read<T>(object, offset);
}
template<class F> F native(std::uintptr_t offset) {
    return reinterpret_cast<F>(g_mainBase + offset);
}
struct Frame { std::uint64_t epoch; std::uint32_t generation; };
Frame currentFrame() {
    const auto epoch = g_epoch.load(std::memory_order_acquire);
    const auto generation = g_generation.load(std::memory_order_relaxed);
    if (!epoch || !generation || g_epoch.load(std::memory_order_acquire) != epoch ||
        pose_render::latchedGeneration(epoch) != generation) return {};
    return {epoch, generation};
}

HOOK_DEFINE_TRAMPOLINE(FilterDrawHook) {
    static u64 Callback(void* filter, void* drawContext, unsigned view, void* mask,
                        int maskCount, void* context, void* buffers) {
        const auto frame = currentFrame();
        const bool sixArguments = palette::profile().sixArgumentFilter;
        void* activeBuffers = sixArguments ? context : buffers;
        if (!frame.epoch || view != 0 || reinterpret_cast<std::uintptr_t>(filter) !=
                g_filter.load(std::memory_order_acquire))
            return Orig(filter, drawContext, view, mask, maskCount, context, buffers);
        if (!filter || !drawContext || !activeBuffers || (!sixArguments && !context)) {
            fail(Failure::Filter); return 0;
        }
        const auto* program = read<const void*>(filter, 8);
        const auto* variations = program ? read<const void*>(program, 8) : nullptr;
        const auto* selected = variations ? read<const void*>(variations, 0x20) : nullptr;
        if (!variations || !read<unsigned>(variations, 0x18) || !selected ||
            read<std::uint16_t>(selected, 0x22) != 1) {
            fail(Failure::Shader); return 0;
        }
        const auto* slot = read<const void*>(reinterpret_cast<void*>(g_mainBase),
                                             palette::profile().sites.primitiveTexturesSlot);
        const auto* primitives = slot ? read<const void*>(slot, 0) : nullptr;
        auto* zero = primitives ? read<void*>(primitives, 8 + 10 * 8) : nullptr;
        if (!zero) { fail(Failure::ZeroTexture); return 0; }
        alignas(16) std::array<std::byte, pure::kMonochromeFilterBytes> input;
        const auto built = pure::buildMonochromeFilter(
            {static_cast<const std::byte*>(filter), input.size()}, input);
        if (built != pure::MonochromeFilterStatus::Ready) {
            fail(Failure::Filter, unsigned(built)); return 0;
        }
        void* pathMask = sixArguments ? native_path::maskForScreenFilter(frame.epoch, frame.generation) : nullptr;
        const auto result = sixArguments ?
            Orig(input.data(), drawContext, view, pathMask ? pathMask : zero, 0, activeBuffers, nullptr) :
            Orig(input.data(), drawContext, view, zero, 0, context, buffers);
        if (pathMask) native_path::finishScreenFilter(pathMask);
        return result;
    }
};

// Best-effort FMAT name for logs: a nearby pointer to a short printable length-prefixed string.
void materialName(const void* resource, char (&out)[48]) {
    out[0] = '?'; out[1] = 0;
    const auto base = reinterpret_cast<std::uintptr_t>(resource);
    if (!base || read<std::uint32_t>(resource, 0) != 0x54414D46u) return;
    for (std::size_t offset = 0x8; offset <= 0x30; offset += 8) {
        const auto pointer = read<std::uintptr_t>(resource, offset);
        if (pointer < base - 0x1000000 || pointer > base + 0x1000000 || (pointer & 1u)) continue;
        const auto* text = reinterpret_cast<const char*>(pointer);
        const auto length = *reinterpret_cast<const std::uint16_t*>(text);
        if (!length || length >= sizeof(out)) continue;
        bool printable = true;
        for (unsigned i = 0; i < length && printable; ++i) printable = text[2 + i] > 0x20 && text[2 + i] < 0x7F;
        if (!printable) continue;
        for (unsigned i = 0; i < length; ++i) out[i] = text[2 + i];
        out[length] = 0;
        return;
    }
}

void logMaterialFailure(const void* model, unsigned materialIndex, const void* resource,
                        const void* shader, int index, const char* check) {
    static std::atomic<unsigned> logged{0};
    if (logged.fetch_add(1, std::memory_order_relaxed) >= 16) return;
    char name[48];
    materialName(resource, name);
    Logging.Log("[self-recall] MONOCHROME_MATERIAL check=%s materials=%u material=%u "
                "name=%s parameter=%d parameters=%u",
                check, unsigned(read<std::uint16_t>(model, palette::profile().model.materialCount)),
                materialIndex, name, index,
                shader ? unsigned(read<std::uint16_t>(shader, 0x4A)) : 0u);
}

bool protectMaterial(void* model, unsigned materialIndex, unsigned buffer) {
    auto* materials = read<std::byte*>(model, palette::profile().model.materials);
    if (!materials) { fail(Failure::Material, materialIndex); return false; }
    auto* material = materials + materialIndex * 0x80;
    const auto* resource = read<const void*>(material, 0);
    const auto* assignment = resource ? read<const void*>(resource, 0x10) : nullptr;
    const auto* shader = assignment ? read<const void*>(assignment, 0) : nullptr;
    const auto* parameters = shader ? read<const std::byte*>(shader, 0x20) : nullptr;
    const auto* mapping = resource ? read<const void*>(resource, 0x60) : nullptr;
    auto* source = read<const std::byte*>(material, 0x48);
    const auto* buffers = read<const std::byte*>(material, 0x40);
    const auto bufferCount = read<std::uint8_t>(material, 0xA);
    if (!shader || !parameters || !mapping || !source || !buffers || !bufferCount ||
        bufferCount > 3 || buffer >= bufferCount || !(read<std::uint16_t>(material, 8) & 1u)) {
        logMaterialFailure(model, materialIndex, resource, shader, -1, "material");
        fail(Failure::Material, materialIndex); return false;
    }
    const char* name = "p_object_attribute";
    const int index = native<int (*)(void*, int, const char**)>(
        palette::profile().sites.findParameter)(model, materialIndex, &name);
    // A modded material without the attribute (Airbender glider) stays filtered instead of cancelling Recall.
    if (index < 0) {
        logMaterialFailure(model, materialIndex, resource, shader, index, "skipped-no-object-attribute");
        return true;
    }
    if (index >= read<std::uint16_t>(shader, 0x4A)) {
        logMaterialFailure(model, materialIndex, resource, shader, index, "object-attribute-range");
        fail(Failure::Parameter, materialIndex << 16); return false;
    }
    const auto* parameter = parameters + index * 0x18;
    const auto sourceOffset = read<std::uint16_t>(parameter, 0x10);
    const auto gpuOffset = read<std::int32_t>(mapping, index * 4);
    const auto gpuBytes = read<std::uint64_t>(material, 0x60);
    if (read<std::uintptr_t>(parameter, 0) || read<std::uint8_t>(parameter, 0x12) != 12 ||
        !palette::detail::floatRange(sourceOffset, read<std::uint16_t>(shader, 0x4C)) ||
        !gpuBytes || gpuBytes > UINT16_MAX || gpuBytes != read<std::uint16_t>(resource, 0xAA) ||
        gpuOffset < 0 || !palette::detail::floatRange(gpuOffset, static_cast<unsigned>(gpuBytes))) {
        logMaterialFailure(model, materialIndex, resource, shader, index, "object-attribute-layout");
        fail(Failure::Parameter, (materialIndex << 16) | unsigned(index)); return false;
    }
    const float original = read<float>(source, sourceOffset);
    float excluded = original;
    const auto encoded = pure::enableMonochromeExclusion(original, excluded);
    if (encoded != pure::ObjectAttributeStatus::Ready) {
        fail(Failure::Attribute, unsigned(encoded)); return false;
    }
    const auto* nativeBuffer = buffers + buffer * 0x48;
    if (!read<std::uintptr_t>(nativeBuffer, 8)) {
        fail(Failure::Upload, (materialIndex << 16) | unsigned(index)); return false;
    }
    const auto set = native<u64 (*)(void*, int, int, float)>(palette::profile().sites.writeSaturation);
    pure::uploadMonochromeAttribute(original, excluded,
        [&](float value) { set(model, materialIndex, index, value); },
        [&] { native<void (*)(void*, unsigned)>(palette::profile().sites.calculateMaterial)(material, buffer); });
    const auto* mapped = native<const std::byte* (*)(const void*)>(
        palette::profile().sites.mapUniform)(nativeBuffer);
    if (!mapped || read<float>(mapped, gpuOffset) != excluded || read<float>(source, sourceOffset) != original) {
        fail(Failure::Upload, (materialIndex << 16) | unsigned(index)); return false;
    }
    return true;
}
}

void initializeForScene(const void* extension) {
    g_filter.store(extension ? read<std::uintptr_t>(extension, 0xAB8) : 0, std::memory_order_release);
}
void beginFrame(std::uint64_t epoch, std::uint32_t generation) {
    g_epoch.store(0, std::memory_order_release);
    g_generation.store(generation, std::memory_order_relaxed);
    g_epoch.store(epoch, std::memory_order_release);
}
void protectHistoricalModel(void* model) {
    const auto frame = currentFrame();
    if (!frame.epoch) return;
    if (pose_render::paletteModel(model, frame.epoch, frame.generation) != pose_render::PaletteModel::Ready)
        return;
    const auto count = read<std::uint16_t>(model, palette::profile().model.materialCount);
    const auto buffer = read<std::uint8_t>(model, palette::profile().model.bufferIndex) & 3u;
    for (unsigned i = 0; i < count; ++i)
        if (!protectMaterial(model, i, buffer)) return;
}
std::uint64_t takeFailure() { return g_failure.exchange(0, std::memory_order_acq_rel); }
void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    FilterDrawHook::InstallAtOffset(palette::profile().sites.drawFilter);
}
}
