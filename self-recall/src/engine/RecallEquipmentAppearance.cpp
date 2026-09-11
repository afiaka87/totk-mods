#include "RecallEquipmentAppearance.hpp"
#include "RecallEquipmentEffects.hpp"
#include "RecallAppearanceHistory.hpp"
#include "RecallOffsets121.hpp"
#include "RecallMemoryProfiler.hpp"
#include "RecallParameterExchange.hpp"
#include "RecallCorpusCapture.hpp"
#if SELF_RECALL_STORAGE_PROFILE == 7
#include "RecallCompressedAppearance.hpp"
#endif
#include <lib.hpp>

namespace self_recall::equipment {
using namespace detail;
using namespace offsets121::equipment_archive;
namespace {
std::uintptr_t g_mainBase = 0;
#if SELF_RECALL_STORAGE_PROFILE == 7
pure::CompressedAppearanceBlobs<pure::kAppearanceBlockCount, pure::kAppearanceStateCapacity> g_appearance;
#else
pure::AppearanceBlobs<pure::kAppearanceBlockCount, pure::kAppearanceStateCapacity> g_appearance;
#endif
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
#if SELF_RECALL_MEMORY_PROFILE
std::atomic<std::uint64_t> g_bodyDifferences{0};
#endif

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
    if constexpr (!pure::kLosslessStorage) return io.field(values);
    if (!io.field(values.count) || values.count > 128) return false;
    return io.transfer(values.scale, sizeof(values.scale)) &&
           io.transfer(values.properties, values.count * sizeof(values.properties[0]));
}
constexpr unsigned kBodyIdentityBytes = 3 * sizeof(std::uintptr_t) + 2 * sizeof(std::uint16_t);
bool bodyIdentity(model::Identity& identity, AppearanceIO& io) {
    return io.field(identity.unit) && io.field(identity.skeleton) && io.field(identity.resource) &&
           io.field(identity.boneCount) && io.field(identity.materialCount);
}

template<class T> T read(const void* p, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
    return value;
}
template<class T> void write(void* p, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(p) + offset, &value, sizeof(value));
}
enum class Parameters { Copy, Validate, Exchange };
bool materialParameters(const model::Identity& model, AppearanceIO& io,
                        Parameters mode = Parameters::Copy) {
    auto* materials = read<std::byte*>(reinterpret_cast<const void*>(model.unit), 0x180);
    if (model.materialCount && !materials) return false;
    for (unsigned i = 0; i < model.materialCount; ++i) {
        auto* material = materials + i * 128;
        const auto* resource = read<const void*>(material, 0);
        if (!resource) return false;
        const auto* assignment = read<const void*>(resource, 0x10);
        const auto* shader = assignment ? read<const void*>(assignment, 0) : nullptr;
        if (!shader) continue;
        const auto bytes = read<std::uint16_t>(shader, 0x4C);
        auto recordedBytes = bytes;
        if (!io.field(recordedBytes) || recordedBytes != bytes) return false;
        auto* parameters = read<void*>(material, 0x48);
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
            reinterpret_cast<Dirty>(g_mainBase + kMarkMaterialParametersDirty)(material);
        }
    }
    return true;
}

} // namespace

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
            corpus::appearance(token, bytes);
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
        // Validate the entire record before the first write, including every material size.
        io.offset = kBodyIdentityBytes;
        if (!materialParameters(live, io, Parameters::Exchange)) break;
#if SELF_RECALL_MEMORY_PROFILE
        if (!g_appearance.equal(token, {upload.data.data(), bytes})) {
            const auto count = g_bodyDifferences.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count <= 8 || count % 1800 == 0)
                Logging.Log("[self-recall] BODY_APPEARANCE_DIFFERENCE count=%llu key=%llu model=%u bytes=%u",
                    static_cast<unsigned long long>(count), static_cast<unsigned long long>(key.serial), modelIndex, bytes);
        }
#endif
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
    corpus::appearance(snapshot, bytes);
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
    if constexpr (!pure::kLosslessStorage) g_appearanceFrames.clear(g_appearance);
    for (auto& token : g_bodyAppearance) { g_appearance.release(token); token = 0; }
    g_appearanceGeneration = historyGeneration;
}

void collectAppearance(const pure::PoseHistory& history) {
    if constexpr (pure::kLosslessStorage)
        g_appearanceFrames.collect(g_appearance, [&](auto key) { return history.canReleaseAppearance(key); });
}

void initializeAppearance(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    g_appearance.initialize();
}
void releaseAppearance(unsigned token) { g_appearance.release(token); }
bool bindAppearanceFrame(pure::PoseFrameKey key, std::span<const unsigned> tokens) {
    const auto bound = g_appearanceFrames.bind(g_appearance, key, tokens);
    if (bound) corpus::binding(key, tokens);
    return bound;
}
unsigned appearanceToken(pure::PoseFrameKey key, unsigned model) {
    return g_appearanceFrames.token(key, model);
}
void logAppearanceMemory() {
#if SELF_RECALL_MEMORY_PROFILE
    const auto& m = g_appearance.memoryUsage();
    Logging.Log("[self-recall] MEM_BODY_APPEARANCE upload_reserved=%llu live_historical_differences=%llu\n",
        static_cast<unsigned long long>(sizeof(g_bodyUploads)),
        static_cast<unsigned long long>(g_bodyDifferences.load(std::memory_order_relaxed)));
    Logging.Log("[self-recall] MEM_APPEARANCE live=%llu peak=%llu states=%u peak_states=%u creates=%llu failures=%llu allocated=%llu peak_allocated=%llu refs=%llu peak_refs=%llu free_blocks_bytes=%u reserved=%llu\n",
        m.liveBytes, m.peakBytes, m.liveStates, m.peakStates, m.creates, m.failures,
        m.liveAllocated, m.peakAllocated,
        m.references, m.peakReferences,
        g_appearance.availableBytes(), static_cast<unsigned long long>(sizeof(g_appearance)));
#endif
}
} // namespace self_recall::equipment
