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
bool g_allocationAttempted = false;
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
}

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
}

namespace self_recall::monochrome {
namespace {
using namespace offsets121::palette;
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
        if (!frame.epoch || view != 0 || reinterpret_cast<std::uintptr_t>(filter) !=
                g_filter.load(std::memory_order_acquire))
            return Orig(filter, drawContext, view, mask, maskCount, context, buffers);
        if (!filter || !drawContext || !context || !buffers) {
            fail(Failure::Filter); return 0;
        }
        const auto* program = read<const void*>(filter, 8);
        const auto* variations = program ? read<const void*>(program, 8) : nullptr;
        const auto* selected = variations ? read<const void*>(variations, 0x20) : nullptr;
        if (!variations || !read<unsigned>(variations, 0x18) || !selected ||
            read<std::uint16_t>(selected, 0x22) != 1) {
            fail(Failure::Shader); return 0;
        }
        const auto* slot = read<const void*>(reinterpret_cast<void*>(g_mainBase), kPrimitiveTexturesSlot);
        const auto* primitives = slot ? read<const void*>(slot, 0) : nullptr;
        auto* zero = primitives ? read<void*>(primitives, 8 + 10 * 8) : nullptr;
        if (!zero) { fail(Failure::ZeroTexture); return 0; }
        alignas(16) std::array<std::byte, pure::kMonochromeFilterBytes> input;
        const auto built = pure::buildMonochromeFilter(
            {static_cast<const std::byte*>(filter), input.size()}, input);
        if (built != pure::MonochromeFilterStatus::Ready) {
            fail(Failure::Filter, unsigned(built)); return 0;
        }
        return Orig(input.data(), drawContext, view, zero, 0, context, buffers);
    }
};

bool protectMaterial(void* model, unsigned materialIndex, unsigned buffer) {
    auto* materials = read<std::byte*>(model, 0x180);
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
        fail(Failure::Material, materialIndex); return false;
    }
    const char* name = "p_object_attribute";
    const int index = native<int (*)(void*, int, const char**)>(kFindMaterialParameter)(model, materialIndex, &name);
    if (index < 0 || index >= read<std::uint16_t>(shader, 0x4A)) {
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
        Logging.Log("[self-recall] MONOCHROME_UPLOAD buffer_missing model=%p material=%u parameter=%d buffer=%u",
            model, materialIndex, index, buffer);
        fail(Failure::Upload, (materialIndex << 16) | unsigned(index)); return false;
    }
    const auto set = native<u64 (*)(void*, int, int, float)>(kWriteSaturation);
    pure::uploadMonochromeAttribute(original, excluded,
        [&](float value) { set(model, materialIndex, index, value); },
        [&] { native<void (*)(void*, unsigned)>(kCalculateMaterial)(material, buffer); });
    const auto* mapped = native<const std::byte* (*)(const void*)>(kMapUniform)(nativeBuffer);
    if (!mapped || read<float>(mapped, gpuOffset) != excluded || read<float>(source, sourceOffset) != original) {
        Logging.Log("[self-recall] MONOCHROME_UPLOAD mismatch model=%p material=%u parameter=%d buffer=%u mapped=%u source=%08x expected_source=%08x gpu=%08x expected_gpu=%08x dirty=%u pending=%u",
            model, materialIndex, index, buffer, unsigned(mapped != nullptr),
            std::bit_cast<unsigned>(read<float>(source, sourceOffset)), std::bit_cast<unsigned>(original),
            mapped ? std::bit_cast<unsigned>(read<float>(mapped, gpuOffset)) : 0,
            std::bit_cast<unsigned>(excluded), unsigned(read<std::uint8_t>(material, 0x2C)),
            unsigned(read<std::uint8_t>(material, 0x2D)));
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
    const auto count = read<std::uint16_t>(model, 0x16A);
    const auto buffer = read<std::uint8_t>(model, 0x15) & 3u;
    for (unsigned i = 0; i < count; ++i)
        if (!protectMaterial(model, i, buffer)) return;
}
std::uint64_t takeFailure() { return g_failure.exchange(0, std::memory_order_acq_rel); }
void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    FilterDrawHook::InstallAtOffset(kDrawMonochromeFilter);
}
}
