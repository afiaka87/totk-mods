#include "RecallEquipmentAppearance.hpp"
#include "RecallEquipmentEffects.hpp"
#include "RecallAppearanceHistory.hpp"
#include "RecallOffsets121.hpp"
#include <lib.hpp>

namespace self_recall::equipment {
using namespace detail;
using namespace offsets121::equipment_archive;
namespace {
std::uintptr_t g_mainBase = 0;
pure::AppearanceBlobs<64u * 1024u * 1024u / 256u, pure::kHistoryCapacity * pure::kPoseModelLimit + 64> g_appearance;
pure::AppearanceFrames<> g_appearanceFrames;
std::uint32_t g_appearanceGeneration = 0;
std::array<std::byte, 65536> g_appearanceScratch{};

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

template<class T> T read(const void* p, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
    return value;
}
template<class T> void write(void* p, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(p) + offset, &value, sizeof(value));
}
bool materialParameters(const model::Identity& model, AppearanceIO& io) {
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
        if (!io.transfer(parameters, bytes)) return false;
        if (io.loading && bytes) {
            using Dirty = void (*)(void*);
            reinterpret_cast<Dirty>(g_mainBase + kMarkMaterialParametersDirty)(material);
        }
    }
    return true;
}

} // namespace

bool captureAppearance(Asset& asset, unsigned assetIndex, std::span<const model::View> source) {
    AppearanceIO io{g_appearanceScratch.data(), static_cast<unsigned>(g_appearanceScratch.size()), 0, false};
    auto index = assetIndex;
    auto root = reinterpret_cast<std::uintptr_t>(asset.root.load());
    equipment_effects::Values effects;
    equipment_effects::captureValues(index, effects);
    if (!io.field(index) || !io.field(root) || !io.field(effects)) return false;
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
    if (!io.field(index) || !io.field(root) || !io.field(effects) || index >= assets.size()) return false;
    auto& asset = assets[index];
    if (asset.life.load(std::memory_order_acquire) != Life::Ready ||
        reinterpret_cast<std::uintptr_t>(asset.root.load()) != root) return false;
    if (!equipment_effects::applyValues(index, effects)) return false;
    for (unsigned i = 0; i < asset.count; ++i) if (!materialParameters(asset.copy[i], io)) return false;
    return io.offset == io.size;
}

void beginRecord(std::uint32_t historyGeneration) {
    if (historyGeneration == g_appearanceGeneration) return;
    g_appearanceFrames.clear(g_appearance);
    g_appearanceGeneration = historyGeneration;
}


void initializeAppearance(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    g_appearance.initialize();
}
void releaseAppearance(unsigned token) { g_appearance.release(token); }
bool bindAppearanceFrame(pure::PoseFrameKey key, std::span<const unsigned> tokens) {
    return g_appearanceFrames.bind(g_appearance, key, tokens);
}
unsigned appearanceToken(pure::PoseFrameKey key, unsigned model) {
    return g_appearanceFrames.token(key, model);
}
} // namespace self_recall::equipment
