#include "RecallOffsets121.hpp"
#include "RecallEquipmentArchive.hpp"
#include "RecallArchiveHeap.hpp"
#include "RecallResourceLease.hpp"
#include "RecallEquipmentEffects.hpp"
#include "RecallArchiveLifecycle.hpp"
#include "RecallEquipmentAppearance.hpp"
#include <array>
#include <atomic>
#include <lib.hpp>

namespace self_recall::equipment {
using namespace detail;
using namespace offsets121::equipment_archive;
namespace {
std::array<Asset, kAssetLimit> g_assets;
std::uintptr_t g_mainBase = 0;
template<class T> T read(const void* p, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
    return value;
}
template<class T> void write(void* p, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(p) + offset, &value, sizeof(value));
}
const void* unitAt(const void* root, unsigned index) {
    const auto* entries = read<const void* const*>(root, 0x28);
    return entries && entries[index] ? read<const void*>(entries[index], 0) : nullptr;
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
    reinterpret_cast<Request>(g_mainBase + kRequestModelRetirement)(asset.root.load(std::memory_order_acquire));
}

std::size_t cloneBudget(const void* root, std::span<const model::View> source) {
    const auto views = read<std::uint8_t>(root, 0x242);
    if (!views || views > 8) return 0;
    std::size_t total = 600 + source.size() * 72;
    constexpr std::size_t kPerUnitReserve = 256u * 1024u;
    const auto* poolSlot = read<const void*>(reinterpret_cast<const void*>(g_mainBase), kGpuPoolManagerSlot);
    const auto* pools = poolSlot ? read<const void*>(poolSlot, 0) : nullptr;
    if (!pools) return 0;
    const auto minimumPool = read<std::size_t>(pools, 0x38);
    for (const auto& view : source) {
        const auto* unit = reinterpret_cast<const void*>(view.identity.unit);
        const auto* resource = read<const void*>(unit, 0x138);
        if (!resource) return 0;
        const auto shapes = read<std::uint16_t>(resource, 0x6A);
        const auto materials = read<std::uint16_t>(resource, 0x6C);
        if (shapes > 512 || materials > pure::kPoseMaterialLimit) return 0;
        const auto* shapeData = read<const std::byte*>(resource, 0x28);
        const auto* materialData = read<const std::byte*>(resource, 0x38);
        if ((shapes && !shapeData) || (materials && !materialData)) return 0;
        alignas(8) std::byte argument[360]{};
        write(argument, 0, resource);
        write<std::uint32_t>(argument, 0x10, 2);
        write<std::uint32_t>(argument, 0x14, 2);
        write<std::uint32_t>(argument, 0x18, 2);
        write<std::uint32_t>(argument, 0x1C, views);
        unsigned meshCount = 1;
        for (unsigned i = 0; i < shapes; ++i) {
            const auto meshes = read<std::uint8_t>(shapeData + i * 96, 0x5B);
            if (meshCount < meshes) meshCount = meshes;
        }
        write(argument, 0x20, meshCount);
        write<std::uint8_t>(argument, 0x30, 1);
        using Calculate = void (*)(void*);
        reinterpret_cast<Calculate>(g_mainBase + kCalculateModelBufferSize)(argument);
        const auto baseBytes = read<std::size_t>(argument, 0x38);
        std::size_t textures = 0;
        for (unsigned i = 0; i < materials; ++i)
            textures += read<std::uint8_t>(materialData + i * 176, 0xA2);
        const auto cpu = baseBytes + 40u * shapes + 40u * textures + 40u * materials +
            ((12u * materials * views + 7u) & ~std::size_t{7}) +
            (112u * shapes + 104u) * views + ((20u * materials + 55u) & 0x3FFFF8u) + 72u * views;
        const auto* block = read<const void*>(unit, 0x210);
        auto gpu = block ? read<std::size_t>(block, 0x30) : 0;
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
    const auto binderCount = read<std::int32_t>(component, 0x38);
    const auto* binders = read<const std::byte*>(component, 0x40);
    const auto resourceCount = read<std::int32_t>(component, 0x58);
    const auto* resources = read<const void* const*>(component, 0x60);
    if (binderCount < 1 || binderCount > int(kResourceLimit) || !binders ||
        resourceCount != int(source.size()) || !resources)
        return refuse("resource_roster", binderCount, resourceCount);
    asset.life.store(Life::Constructing, std::memory_order_release);
    for (int i = 0; i < binderCount; ++i) {
        if (!asset.resources[i].retain(g_mainBase, binders + i * 40)) {
            releaseResources(asset);
            asset.life.store(Life::Empty, std::memory_order_release);
            return refuse("resource_lease", i, binderCount);
        }
        ++asset.resourceCount;
    }
    for (unsigned i = 0; i < source.size(); ++i) {
        bool owned = false;
        for (unsigned j = 0; j < asset.resourceCount; ++j)
            owned |= read<const void*>(asset.resources[j].resource(), 0x28) == resources[i];
        if (!resources[i] || !owned) {
            releaseResources(asset);
            asset.life.store(Life::Empty, std::memory_order_release);
            return refuse("resource_owner", i, asset.resourceCount);
        }
    }
    struct CreateArgument {
        std::uint32_t count, views, flags = 0x01000000, pad = 0;
        std::uint64_t owner = 0;
    } argument{static_cast<std::uint32_t>(source.size()), read<std::uint8_t>(root, 0x242)};
    static_assert(sizeof(CreateArgument) == 24);
    struct MemBuffer { std::uintptr_t vtable; void* cpu; void* gpu; void* extra; };
    const auto bufferVtable = read<std::uintptr_t>(reinterpret_cast<const void*>(g_mainBase), kMemoryBufferVtableSlot) + 16;
    MemBuffer buffer{bufferVtable, nullptr, nullptr, nullptr};
    using Initialize = void (*)(void*, void*, void*, void*);
    const auto initialize = reinterpret_cast<Initialize>(g_mainBase + kInitializeMemoryBuffer);
    initialize(&buffer, heap, heap, nullptr);
    const char* name = "SelfRecallEquipment";
    using Create = void* (*)(const char* const*, const void*, void*);
    auto* copy = reinterpret_cast<Create>(g_mainBase + kCreateModelRoot)(&name, &argument, &buffer);
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
        const auto vtable = read<std::uintptr_t>(unit, 0);
        const auto getName = read<Name>(reinterpret_cast<const void*>(vtable), 0x28);
        const auto* modelName = getName(unit);
        initialize(&buffer, heap, heap, nullptr);
        const auto* cloned = reinterpret_cast<Push>(g_mainBase + kAppendModelResource)(copy, resources[i], &modelName, &buffer);
        model::View view;
        complete = model::describe(g_mainBase, cloned, view) == model::ViewStatus::Ready &&
            view.identity.resource == source[i].identity.resource &&
            view.identity.boneCount == source[i].identity.boneCount &&
            view.identity.materialCount == source[i].identity.materialCount;
        asset.source[i] = source[i].identity;
        asset.copy[i] = view.identity;
    }
    using Configure = void (*)(const void*, void*);
    using Bind = void (*)(void*, const void*);
    if (complete) {
        reinterpret_cast<Configure>(g_mainBase + kConfigureModelViews)(playerComponent, copy);
        reinterpret_cast<Configure>(g_mainBase + kConfigureModelDrawFlags)(playerComponent, copy);
        reinterpret_cast<Bind>(g_mainBase + kBindModelScene)(copy, read<const void*>(root, 0x60));
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
    g_mainBase = mainBase;
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
    const auto* root = read<const void*>(component, 0x28);
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
            read<const void*>(component, 0x20), match->root.load(std::memory_order_acquire)))
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
            if (!ownedRoot || read<const void*>(ownedRoot, 0x60) != scene)
                return refuse("scene", token.unit, asset.world);
            const auto* unit = unitAt(ownedRoot, i);
            if (model::describe(g_mainBase, unit, view) != model::ViewStatus::Ready ||
                view.identity != asset.copy[i] || token.skeleton != view.identity.skeleton ||
                token.resource != view.identity.resource || token.boneCount != view.identity.boneCount ||
                token.materialCount != view.identity.materialCount)
                return refuse("identity", token.unit, i);
            using Configure = void (*)(const void*, const void*);
            reinterpret_cast<Configure>(g_mainBase + kConfigureModelViews)(playerComponent, ownedRoot);
            root = ownedRoot;
            return true;
        }
    }
    return refuse("missing", token.unit, token.resource);
}
} // namespace self_recall::equipment
