#include "RecallRuntimeEngine.hpp"
#include "RecallModelEngine.hpp"
#include "RecallEffectsEngine.hpp"
#include "RecallRender.hpp"
#include "RecallAppearance.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <lib.hpp>

namespace self_recall::equipment {
namespace {
alignas(4096) std::byte g_arena[kArchiveHeapBytes];
std::atomic<void*> g_heap{nullptr};
std::uintptr_t g_heapMainBase = 0;

HOOK_DEFINE_TRAMPOLINE(FindArchiveHeapHook) {
    static void* Callback(void* manager, const void* allocation) {
        if (archiveOwns(allocation)) return g_heap.load(std::memory_order_acquire);
        return Orig(manager, allocation);
    }
};
}

bool archiveOwns(const void* address) {
    const auto p = reinterpret_cast<std::uintptr_t>(address);
    const auto start = reinterpret_cast<std::uintptr_t>(g_arena);
    return g_heap.load(std::memory_order_acquire) && p >= start && p - start < sizeof(g_arena);
}

void installHeap(std::uintptr_t mainBase) {
    g_heapMainBase = mainBase;
    FindArchiveHeapHook::InstallAtOffset(0x007FE124);
}

void* archiveHeap() {
    auto* heap = g_heap.load(std::memory_order_acquire);
    if (heap || !g_heapMainBase) return heap;
    const char* name = "SelfRecallEquipment";
    using Create = void* (*)(void*, std::size_t, const char* const*, bool, void*);
    heap = reinterpret_cast<Create>(g_heapMainBase + 0x00E7CF70)(g_arena, sizeof(g_arena), &name, true, nullptr);
    g_heap.store(heap, std::memory_order_release);
    Logging.Log("[self-recall] EQUIPMENT_HEAP ready=%u bytes=%llu", unsigned(heap != nullptr),
                static_cast<unsigned long long>(sizeof(g_arena)));
    return heap;
}

std::size_t contiguousArchiveBytes() {
    const auto* heap = g_heap.load(std::memory_order_acquire);
    if (!heap) return 0;
    using Size = std::size_t (*)(const void*, int);
    return reinterpret_cast<Size>(g_heapMainBase + 0x00D8A12C)(heap, 4096);
}

void* allocateArchive(std::size_t bytes) {
    auto* heap = archiveHeap();
    if (!heap || !bytes) return nullptr;
    using Allocate = void* (*)(void*, std::size_t, int);
    return reinterpret_cast<Allocate>(g_heapMainBase + 0x00B6E8E4)(heap, bytes, 8);
}

void freeArchive(void* address) {
    if (!address || !archiveOwns(address)) return;
    using Free = void (*)(void*, void*);
    reinterpret_cast<Free>(g_heapMainBase + 0x02A294DC)(g_heap.load(std::memory_order_acquire), address);
}
}
namespace self_recall::equipment {
using namespace detail;
using namespace offsets121::equipment_archive;
namespace {
std::uintptr_t g_appearanceMainBase = 0;
pure::CompressedAppearanceBlobs<pure::kAppearanceBlockCount, pure::kAppearanceStateCapacity> g_appearance;
pure::AppearanceFrames<> g_appearanceFrames;
std::uint32_t g_appearanceGeneration = 0;
std::array<std::byte, 65536> g_appearanceScratch{};
std::array<unsigned, pure::kPoseModelLimit> g_bodyAppearance{};
struct BodyUpload {
    std::atomic<bool> busy{false};
    model::Identity identity{};
    unsigned bytes = 0;
    unsigned modelIndex = 0;
    std::array<std::byte, 65536> data{};
};
std::array<BodyUpload, pure::kPoseReadBufferCount> g_bodyUploads;
std::array<std::atomic<bool>, pure::kPoseModelLimit> g_bodyBusy{};

struct AppearanceIO {
    std::byte* data;
    unsigned size, offset = 0;
    bool loading;
    bool transfer(void* bytes, unsigned count) {
        if (offset > size || count > size - offset) return false;
        if (!count) return true;
        if (!bytes) return false;
        if (loading) std::memcpy(bytes, data + offset, count);
        else std::memcpy(data + offset, bytes, count);
        offset += count;
        return true;
    }
    template<class T> bool field(T& value) { return transfer(&value, sizeof(value)); }
};

bool effectValues(equipment_effects::Values& values, AppearanceIO& io) {
    if (!io.field(values.count) || values.count > 128) return false;
    return io.transfer(values.scale, sizeof(values.scale)) &&
           io.transfer(values.properties, values.count * sizeof(values.properties[0]));
}
constexpr unsigned kBodyIdentityBytes = 3 * sizeof(std::uintptr_t) + 2 * sizeof(std::uint16_t);
bool bodyIdentity(model::Identity& identity, AppearanceIO& io) {
    return io.field(identity.unit) && io.field(identity.skeleton) && io.field(identity.resource) &&
           io.field(identity.boneCount) && io.field(identity.materialCount);
}

template<class T> T readAppearance(const void* p, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
    return value;
}
enum class Parameters { Copy, Validate, Exchange };
bool materialParameters(const model::Identity& model, AppearanceIO& io,
                        Parameters mode = Parameters::Copy) {
    auto* materials = readAppearance<std::byte*>(reinterpret_cast<const void*>(model.unit), 0x180);
    if (model.materialCount && !materials) return false;
    for (unsigned i = 0; i < model.materialCount; ++i) {
        auto* material = materials + i * 128;
        const auto* resource = readAppearance<const void*>(material, 0);
        if (!resource) return false;
        const auto* assignment = readAppearance<const void*>(resource, 0x10);
        const auto* shader = assignment ? readAppearance<const void*>(assignment, 0) : nullptr;
        if (!shader) continue;
        const auto bytes = readAppearance<std::uint16_t>(shader, 0x4C);
        auto recordedBytes = bytes;
        if (!io.field(recordedBytes) || recordedBytes != bytes) return false;
        auto* parameters = readAppearance<void*>(material, 0x48);
        if (bytes && !parameters) return false;
        if (io.offset > io.size || bytes > io.size - io.offset) return false;
        if (mode == Parameters::Exchange) {
            if (!pure::exchangeParameterBytes({static_cast<std::byte*>(parameters), bytes},
                                              {io.data + io.offset, bytes})) return false;
            io.offset += bytes;
        } else if (mode == Parameters::Validate) io.offset += bytes;
        else if (!io.transfer(parameters, bytes)) return false;
        if (io.loading && bytes && mode != Parameters::Validate) {
            using Dirty = void (*)(void*);
            reinterpret_cast<Dirty>(g_appearanceMainBase + kMarkMaterialParametersDirty)(material);
        }
    }
    return true;
}

}

bool captureBodyAppearance(const pure::RecordedPoseFrame& frame, std::span<unsigned> tokens) {
    if (tokens.size() < frame.header.bodyModelCount) return false;
    for (unsigned i = 0; i < frame.header.bodyModelCount; ++i) {
        const auto& recorded = frame.models[i].identity;
        model::Identity identity{recorded.unit, recorded.skeleton, recorded.resource,
                                 recorded.boneCount, recorded.materialCount};
        AppearanceIO io{g_appearanceScratch.data(), unsigned(g_appearanceScratch.size()), 0, false};
        if (!bodyIdentity(identity, io) || !materialParameters(identity, io))
            return refuseAppearance("body_appearance_capture", i, frame.header.key.serial);
        const std::span<const std::byte> bytes{io.data, io.offset};
        if (!g_appearance.equal(g_bodyAppearance[i], bytes)) {
            const auto token = g_appearance.create(bytes);
            if (!token) return refuseAppearance("body_appearance_capacity", io.offset, g_appearance.availableBytes());
            g_appearance.release(g_bodyAppearance[i]);
            g_bodyAppearance[i] = token;
        }
        tokens[i] = g_bodyAppearance[i];
    }
    return true;
}

BodyAppearanceScope::BodyAppearanceScope(pure::PoseFrameKey key, unsigned modelIndex,
                                        const model::Identity& live) {
    bool availableModel = false;
    if (modelIndex >= g_bodyBusy.size() || !g_bodyBusy[modelIndex].compare_exchange_strong(
            availableModel, true, std::memory_order_acquire)) {
        refuseAppearance("body_appearance_busy", modelIndex, key.serial); return;
    }
    struct ModelUnlock {
        unsigned index; bool keep = false;
        ~ModelUnlock() { if (!keep) g_bodyBusy[index].store(false, std::memory_order_release); }
    } modelUnlock{modelIndex};
    const auto token = appearanceToken(key, modelIndex);
    const auto bytes = g_appearance.size(token);
    if (!bytes || bytes > g_appearanceScratch.size()) {
        refuseAppearance("body_appearance_frame", modelIndex, key.serial); return;
    }
    for (unsigned i = 0; i < g_bodyUploads.size(); ++i) {
        auto& upload = g_bodyUploads[i];
        bool available = false;
        if (!upload.busy.compare_exchange_strong(available, true, std::memory_order_acquire)) continue;
        struct Unlock {
            BodyUpload& upload; bool keep = false;
            ~Unlock() { if (!keep) upload.busy.store(false, std::memory_order_release); }
        } unlock{upload};
        if (!g_appearance.copy(token, {upload.data.data(), bytes})) break;
        AppearanceIO io{upload.data.data(), bytes, 0, true};
        model::Identity recorded{};
        if (!bodyIdentity(recorded, io) || recorded != live ||
            !materialParameters(live, io, Parameters::Validate) || io.offset != bytes) break;
        upload.identity = live;
        upload.bytes = bytes;
        upload.modelIndex = modelIndex;
        // Validate the full record before writing any material parameter.
        io.offset = kBodyIdentityBytes;
        if (!materialParameters(live, io, Parameters::Exchange)) break;
        buffer_ = int(i);
        unlock.keep = true;
        modelUnlock.keep = true;
        return;
    }
    refuseAppearance("body_appearance_upload", modelIndex, key.serial);
}

BodyAppearanceScope::~BodyAppearanceScope() {
    if (buffer_ < 0) return;
    auto& upload = g_bodyUploads[buffer_];
    AppearanceIO io{upload.data.data(), upload.bytes, kBodyIdentityBytes, true};
    if (!materialParameters(upload.identity, io, Parameters::Exchange))
        refuseAppearance("body_appearance_restore", buffer_, upload.bytes);
    g_bodyBusy[upload.modelIndex].store(false, std::memory_order_release);
    upload.busy.store(false, std::memory_order_release);
}

bool captureAppearance(Asset& asset, unsigned assetIndex, std::span<const model::View> source) {
    AppearanceIO io{g_appearanceScratch.data(), static_cast<unsigned>(g_appearanceScratch.size()), 0, false};
    auto index = assetIndex;
    auto root = reinterpret_cast<std::uintptr_t>(asset.root.load());
    equipment_effects::Values effects;
    equipment_effects::captureValues(index, effects);
    if (!io.field(index) || !io.field(root) || !effectValues(effects, io)) return false;
    for (const auto& model : source) if (!materialParameters(model.identity, io)) return false;
    const std::span<const std::byte> bytes{io.data, io.offset};
    if (g_appearance.equal(asset.appearance, bytes)) return true;
    const auto snapshot = g_appearance.create(bytes);
    if (!snapshot) return refuseAppearance("appearance_capacity", io.offset, g_appearance.availableBytes());
    if (!asset.appearance)
        Logging.Log("[self-recall] EQUIPMENT_APPEARANCE asset=%u bytes=%u properties=%u free=%u",
            index, io.offset, effects.count, g_appearance.availableBytes());
    g_appearance.release(asset.appearance);
    asset.appearance = snapshot;
    return true;
}

bool restoreAppearance(unsigned token, std::span<Asset> assets) {
    const auto size = g_appearance.size(token);
    if (!size || size > g_appearanceScratch.size() ||
        !g_appearance.copy(token, {g_appearanceScratch.data(), size})) return false;
    AppearanceIO io{g_appearanceScratch.data(), size, 0, true};
    unsigned index = 0;
    std::uintptr_t root = 0;
    equipment_effects::Values effects;
    if (!io.field(index) || !io.field(root) || !effectValues(effects, io) || index >= assets.size()) return false;
    auto& asset = assets[index];
    if (asset.life.load(std::memory_order_acquire) != Life::Ready ||
        reinterpret_cast<std::uintptr_t>(asset.root.load()) != root) return false;
    if (!equipment_effects::applyValues(index, effects)) return false;
    for (unsigned i = 0; i < asset.count; ++i) if (!materialParameters(asset.copy[i], io)) return false;
    return io.offset == io.size;
}

void beginRecord(std::uint32_t historyGeneration) {
    if (historyGeneration == g_appearanceGeneration) return;
    for (auto& token : g_bodyAppearance) { g_appearance.release(token); token = 0; }
    g_appearanceGeneration = historyGeneration;
}

void collectAppearance(const pure::PoseHistory& history) {
    g_appearanceFrames.collect(g_appearance, [&](auto key) { return history.canReleaseAppearance(key); });
}

void initializeAppearance(std::uintptr_t mainBase) {
    g_appearanceMainBase = mainBase;
    g_appearance.initialize();
}
void releaseAppearance(unsigned token) { g_appearance.release(token); }
bool bindAppearanceFrame(pure::PoseFrameKey key, std::span<const unsigned> tokens) {
    return g_appearanceFrames.bind(g_appearance, key, tokens);
}
unsigned appearanceToken(pure::PoseFrameKey key, unsigned model) {
    return g_appearanceFrames.token(key, model);
}
}

namespace self_recall::equipment {
using namespace detail;
using namespace offsets121::equipment_archive;
namespace {
std::array<Asset, kAssetLimit> g_assets;
std::uintptr_t g_archiveMainBase = 0;
template<class T> T readArchive(const void* p, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
    return value;
}
template<class T> void writeArchive(void* p, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(p) + offset, &value, sizeof(value));
}
const void* unitAt(const void* root, unsigned index) {
    const auto* entries = readArchive<const void* const*>(root, 0x28);
    return entries && entries[index] ? readArchive<const void*>(entries[index], 0) : nullptr;
}
bool refuse(const char* reason, std::uint64_t a = 0, std::uint64_t b = 0) {
    static std::uint64_t count = 0;
    if (++count <= 16 || count % 300 == 0)
        Logging.Log("[self-recall] EQUIPMENT_ARCHIVE_REFUSED reason=%s a=%llu b=%llu count=%llu",
            reason, static_cast<unsigned long long>(a), static_cast<unsigned long long>(b),
            static_cast<unsigned long long>(count));
    return false;
}

Asset* prepareModelDestruction(void* root) {
    Asset* owned = nullptr;
    if (archiveOwns(root)) for (auto& asset : g_assets) {
        if (asset.root.load(std::memory_order_acquire) == root) { owned = &asset; break; }
    }
    if (owned) equipment_effects::retire(static_cast<unsigned>(owned - g_assets.data()));

    return owned;
}

HOOK_DEFINE_TRAMPOLINE(ModelDestroyedHook) {
    static std::uintptr_t Callback(void* root) {
        auto* owned = prepareModelDestruction(root);
        const auto result = Orig(root);

        if (owned) owned->life.store(Life::Destroyed, std::memory_order_release);
        return result;
    }
};

void releaseResources(Asset& asset) {
    for (auto& resource : asset.resources) resource.release();
    asset.resourceCount = 0;
}
void retire(Asset& asset) {
    if (asset.life.load(std::memory_order_acquire) != Life::Ready) return;
    asset.life.store(Life::Retiring, std::memory_order_release);
    releaseAppearance(asset.appearance);
    asset.appearance = 0;
    equipment_effects::retire(static_cast<unsigned>(&asset - g_assets.data()));
    using Request = void (*)(const void*);
    reinterpret_cast<Request>(g_archiveMainBase + kRequestModelRetirement)(asset.root.load(std::memory_order_acquire));
}

std::size_t cloneBudget(const void* root, std::span<const model::View> source) {
    const auto views = readArchive<std::uint8_t>(root, 0x242);
    if (!views || views > 8) return 0;
    std::size_t total = 600 + source.size() * 72;
    constexpr std::size_t kPerUnitReserve = 256u * 1024u;
    const auto* poolSlot = readArchive<const void*>(reinterpret_cast<const void*>(g_archiveMainBase), kGpuPoolManagerSlot);
    const auto* pools = poolSlot ? readArchive<const void*>(poolSlot, 0) : nullptr;
    if (!pools) return 0;
    const auto minimumPool = readArchive<std::size_t>(pools, 0x38);
    for (const auto& view : source) {
        const auto* unit = reinterpret_cast<const void*>(view.identity.unit);
        const auto* resource = readArchive<const void*>(unit, 0x138);
        if (!resource) return 0;
        const auto shapes = readArchive<std::uint16_t>(resource, 0x6A);
        const auto materials = readArchive<std::uint16_t>(resource, 0x6C);
        if (shapes > 512 || materials > pure::kPoseMaterialLimit) return 0;
        const auto* shapeData = readArchive<const std::byte*>(resource, 0x28);
        const auto* materialData = readArchive<const std::byte*>(resource, 0x38);
        if ((shapes && !shapeData) || (materials && !materialData)) return 0;
        alignas(8) std::byte argument[360]{};
        writeArchive(argument, 0, resource);
        writeArchive<std::uint32_t>(argument, 0x10, 2);
        writeArchive<std::uint32_t>(argument, 0x14, 2);
        writeArchive<std::uint32_t>(argument, 0x18, 2);
        writeArchive<std::uint32_t>(argument, 0x1C, views);
        unsigned meshCount = 1;
        for (unsigned i = 0; i < shapes; ++i) {
            const auto meshes = readArchive<std::uint8_t>(shapeData + i * 96, 0x5B);
            if (meshCount < meshes) meshCount = meshes;
        }
        writeArchive(argument, 0x20, meshCount);
        writeArchive<std::uint8_t>(argument, 0x30, 1);
        using Calculate = void (*)(void*);
        reinterpret_cast<Calculate>(g_archiveMainBase + kCalculateModelBufferSize)(argument);
        const auto baseBytes = readArchive<std::size_t>(argument, 0x38);
        std::size_t textures = 0;
        for (unsigned i = 0; i < materials; ++i)
            textures += readArchive<std::uint8_t>(materialData + i * 176, 0xA2);
        const auto cpu = baseBytes + 40u * shapes + 40u * textures + 40u * materials +
            ((12u * materials * views + 7u) & ~std::size_t{7}) +
            (112u * shapes + 104u) * views + ((20u * materials + 55u) & 0x3FFFF8u) + 72u * views;
        const auto* block = readArchive<const void*>(unit, 0x210);
        auto gpu = block ? readArchive<std::size_t>(block, 0x30) : 0;
        if (baseBytes > kArchiveHeapBytes || cpu > kArchiveHeapBytes ||
            gpu > kArchiveHeapBytes / 3 || minimumPool > kArchiveHeapBytes) return 0;
        gpu *= 3;
        if (gpu < minimumPool) gpu = minimumPool;
        total += cpu * 2 + gpu + kPerUnitReserve;
        if (total > kArchiveHeapBytes) return 0;
    }
    return total;
}

bool create(Asset& asset, const void* component, const void* root,
            std::uint32_t actorId, std::uint32_t world, const void* playerComponent,
            std::span<const model::View> source) {
    auto* heap = archiveHeap();
    const auto budget = heap ? cloneBudget(root, source) : 0;
    const auto available = contiguousArchiveBytes();
    if (!budget || budget > available) return refuse("budget", budget, available);
    const auto binderCount = readArchive<std::int32_t>(component, 0x38);
    const auto* binders = readArchive<const std::byte*>(component, 0x40);
    const auto resourceCount = readArchive<std::int32_t>(component, 0x58);
    const auto* resources = readArchive<const void* const*>(component, 0x60);
    if (binderCount < 1 || binderCount > int(kResourceLimit) || !binders ||
        resourceCount != int(source.size()) || !resources)
        return refuse("resource_roster", binderCount, resourceCount);
    asset.life.store(Life::Constructing, std::memory_order_release);
    for (int i = 0; i < binderCount; ++i) {
        if (!asset.resources[i].retain(g_archiveMainBase, binders + i * 40)) {
            releaseResources(asset);
            asset.life.store(Life::Empty, std::memory_order_release);
            return refuse("resource_lease", i, binderCount);
        }
        ++asset.resourceCount;
    }
    for (unsigned i = 0; i < source.size(); ++i) {
        bool owned = false;
        for (unsigned j = 0; j < asset.resourceCount; ++j)
            owned |= readArchive<const void*>(asset.resources[j].resource(), 0x28) == resources[i];
        if (!resources[i] || !owned) {
            releaseResources(asset);
            asset.life.store(Life::Empty, std::memory_order_release);
            return refuse("resource_owner", i, asset.resourceCount);
        }
    }
    struct CreateArgument {
        std::uint32_t count, views, flags = 0x01000000, pad = 0;
        std::uint64_t owner = 0;
    } argument{static_cast<std::uint32_t>(source.size()), readArchive<std::uint8_t>(root, 0x242)};
    static_assert(sizeof(CreateArgument) == 24);
    struct MemBuffer { std::uintptr_t vtable; void* cpu; void* gpu; void* extra; };
    const auto bufferVtable = readArchive<std::uintptr_t>(reinterpret_cast<const void*>(g_archiveMainBase), kMemoryBufferVtableSlot) + 16;
    MemBuffer buffer{bufferVtable, nullptr, nullptr, nullptr};
    using Initialize = void (*)(void*, void*, void*, void*);
    const auto initialize = reinterpret_cast<Initialize>(g_archiveMainBase + kInitializeMemoryBuffer);
    initialize(&buffer, heap, heap, nullptr);
    const char* name = "SelfRecallEquipment";
    using Create = void* (*)(const char* const*, const void*, void*);
    auto* copy = reinterpret_cast<Create>(g_archiveMainBase + kCreateModelRoot)(&name, &argument, &buffer);
    if (!copy) {
        releaseResources(asset);
        asset.life.store(Life::Empty, std::memory_order_release);
        return refuse("create_root", actorId, source.size());
    }
    asset.root.store(copy, std::memory_order_release);
    asset.sourceRoot = root;
    asset.actorId = actorId;
    asset.world = world;
    asset.count = static_cast<std::uint16_t>(source.size());
    asset.last = {};
    using Name = const char* (*)(const void*);
    using Push = void* (*)(void*, const void*, const char* const*, void*);
    bool complete = copy != nullptr;
    for (unsigned i = 0; complete && i < source.size(); ++i) {
        const auto* unit = reinterpret_cast<const void*>(source[i].identity.unit);
        const auto vtable = readArchive<std::uintptr_t>(unit, 0);
        const auto getName = readArchive<Name>(reinterpret_cast<const void*>(vtable), 0x28);
        const auto* modelName = getName(unit);
        initialize(&buffer, heap, heap, nullptr);
        const auto* cloned = reinterpret_cast<Push>(g_archiveMainBase + kAppendModelResource)(copy, resources[i], &modelName, &buffer);
        model::View view;
        complete = model::describe(g_archiveMainBase, cloned, view) == model::ViewStatus::Ready &&
            view.identity.resource == source[i].identity.resource &&
            view.identity.boneCount == source[i].identity.boneCount &&
            view.identity.materialCount == source[i].identity.materialCount;
        asset.source[i] = source[i].identity;
        asset.copy[i] = view.identity;
    }
    using Configure = void (*)(const void*, void*);
    using Bind = void (*)(void*, const void*);
    if (complete) {
        reinterpret_cast<Configure>(g_archiveMainBase + kConfigureModelViews)(playerComponent, copy);
        reinterpret_cast<Configure>(g_archiveMainBase + kConfigureModelDrawFlags)(playerComponent, copy);
        reinterpret_cast<Bind>(g_archiveMainBase + kBindModelScene)(copy, readArchive<const void*>(root, 0x60));
    }
    asset.life.store(Life::Ready, std::memory_order_release);
    if (!complete) { retire(asset); return refuse("clone_identity", actorId, source.size()); }
    Logging.Log("[self-recall] EQUIPMENT_ARCHIVED actor=%u models=%u budget=%llu consumed=%llu",
        actorId, asset.count, static_cast<unsigned long long>(budget),
        static_cast<unsigned long long>(available - contiguousArchiveBytes()));
    return true;
}
}

bool refuseAppearance(const char* reason, std::uint64_t detail, std::uint64_t extra) {
    return refuse(reason, detail, extra);
}

void install(std::uintptr_t mainBase) {
    g_archiveMainBase = mainBase;
    initializeAppearance(mainBase);
    installHeap(mainBase);
    equipment_effects::install(mainBase);
    ModelDestroyedHook::InstallAtOffset(kDestroyModelDependencies);
}

bool publishedModel(const void* unit) {
    if (!archiveOwns(unit)) return false;
    for (const auto& asset : g_assets) {
        if (!pure::archiveSuppressesUnselectedShapes(asset.life.load(std::memory_order_acquire))) continue;
        for (unsigned i = 0; i < asset.count; ++i)
            if (asset.copy[i].unit == reinterpret_cast<std::uintptr_t>(unit)) return true;
    }
    return false;
}

bool remap(const void* component, std::uint32_t actorId, std::uint32_t world,
           const void* playerComponent, std::span<model::View> source) {
    if (!component || !playerComponent || source.empty() || source.size() > pure::kPoseModelLimit)
        return refuse("source", source.size(), world);
    const auto* root = readArchive<const void*>(component, 0x28);
    Asset* match = nullptr;
    Asset* empty = nullptr;
    for (auto& asset : g_assets) {
        const auto life = asset.life.load(std::memory_order_acquire);
        if (life == Life::Empty && !empty) empty = &asset;
        if (life != Life::Ready || asset.world != world || asset.actorId != actorId ||
            asset.sourceRoot != root || asset.count != source.size()) continue;
        bool same = true;
        for (unsigned i = 0; i < source.size(); ++i) same &= asset.source[i] == source[i].identity;
        if (same) { match = &asset; break; }
    }
    if (!match) {
        if (!empty) return refuse("capacity", kAssetLimit, world);
        if (!create(*empty, component, root, actorId, world, playerComponent, source)) return false;
        match = empty;
    }
    if (!equipment_effects::retain(static_cast<unsigned>(match - g_assets.data()),
            readArchive<const void*>(component, 0x20), match->root.load(std::memory_order_acquire)))
        return refuse("effects", actorId, 0);
    if (!captureAppearance(*match, static_cast<unsigned>(match - g_assets.data()), source)) return refuse("appearance_capture", actorId, source.size());
    for (unsigned i = 0; i < source.size(); ++i) {
        source[i].identity = match->copy[i];
        source[i].pose.identity.unit = match->copy[i].unit;
        source[i].pose.identity.skeleton = match->copy[i].skeleton;
        source[i].pose.identity.resource = match->copy[i].resource;
    }
    return true;
}

bool recorded(const pure::RecordedPoseFrame& frame) {
    beginRecord(frame.header.key.generation);
    std::array<unsigned, pure::kPoseModelLimit> tokens{};
    if (!captureBodyAppearance(frame, tokens)) return false;
    for (auto& asset : g_assets) {
        if (asset.life.load(std::memory_order_acquire) != Life::Ready) continue;
        for (unsigned i = frame.header.bodyModelCount; i < frame.header.modelCount; ++i)
            for (unsigned j = 0; j < asset.count; ++j)
                if (frame.models[i].identity.unit == asset.copy[j].unit) {
                    asset.last = frame.header.key;
                    tokens[i] = asset.appearance;
                }
    }
    for (unsigned i = frame.header.bodyModelCount; i < frame.header.modelCount; ++i)
        if (!tokens[i]) return refuse("appearance_missing", i, frame.header.key.serial);
    return bindAppearanceFrame(frame.header.key, {tokens.data(), frame.header.modelCount});
}

bool selectAppearance(const pure::RecordedPoseFrame& frame) {
    unsigned restored[pure::kPoseModelLimit]{};
    unsigned count = 0;
    for (unsigned i = frame.header.bodyModelCount; i < frame.header.modelCount; ++i) {
        const auto token = appearanceToken(frame.header.key, i);
        if (!token) return refuse("appearance_frame", i, frame.header.key.serial);
        bool duplicate = false;
        for (unsigned j = 0; j < count; ++j) duplicate |= restored[j] == token;
        if (duplicate) continue;
        if (!restoreAppearance(token, g_assets)) return refuse("appearance_restore", token, i);
        restored[count++] = token;
    }
    return true;
}

void recordEffects(pure::PoseFrameHeader& header, std::span<const model::View> views) {
    for (unsigned a = 0; a < g_assets.size(); ++a) {
        const auto& asset = g_assets[a];
        if (asset.life.load(std::memory_order_acquire) != Life::Ready || !asset.count) continue;
        for (const auto& view : views) if (view.identity.unit == asset.copy[0].unit) {
            equipment_effects::record(a, header);
            break;
        }
    }
}

void collectExpired(const pure::PoseHistory& history, std::uint32_t world) {
    collectAppearance(history);
    for (auto& asset : g_assets) {
        const auto life = asset.life.load(std::memory_order_acquire);
        if (life == Life::Destroyed) {
            releaseResources(asset);
            asset.root.store(nullptr, std::memory_order_release);
            asset.life.store(Life::Empty, std::memory_order_release);
        } else if (life == Life::Ready &&
                   (asset.world != world || !asset.last || !history.contains(asset.last))) {
            retire(asset);
        }
    }
}

bool resolve(const pure::RecordedModelIdentity& token, const void* scene,
             const void* playerComponent, model::View& view, const void*& root) {
    for (auto& asset : g_assets) {
        if (asset.life.load(std::memory_order_acquire) != Life::Ready) continue;
        for (unsigned i = 0; i < asset.count; ++i) {
            if (asset.copy[i].unit != token.unit) continue;
            const auto* ownedRoot = asset.root.load(std::memory_order_acquire);
            if (!ownedRoot || readArchive<const void*>(ownedRoot, 0x60) != scene)
                return refuse("scene", token.unit, asset.world);
            const auto* unit = unitAt(ownedRoot, i);
            if (model::describe(g_archiveMainBase, unit, view) != model::ViewStatus::Ready ||
                view.identity != asset.copy[i] || token.skeleton != view.identity.skeleton ||
                token.resource != view.identity.resource || token.boneCount != view.identity.boneCount ||
                token.materialCount != view.identity.materialCount)
                return refuse("identity", token.unit, i);
            using Configure = void (*)(const void*, const void*);
            reinterpret_cast<Configure>(g_archiveMainBase + kConfigureModelViews)(playerComponent, ownedRoot);
            root = ownedRoot;
            return true;
        }
    }
    return refuse("missing", token.unit, token.resource);
}
}
