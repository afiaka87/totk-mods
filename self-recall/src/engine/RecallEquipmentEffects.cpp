#include "RecallOffsets121.hpp"
#include "RecallEquipmentEffects.hpp"
#include "RecallNativeEffectSchema.hpp"
#include "RecallArchiveHeap.hpp"
#include "RecallPoseRender.hpp"
#include "RecallPoseSession.hpp"
#include "RecallWristProvider.hpp"
#include "RecallFrameHooks.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <optional>
#include <lib.hpp>

namespace self_recall::equipment_effects {
using namespace offsets121::equipment_effects;
namespace {
constexpr unsigned kAssets = 64, kMasks = 256;
using detail::kProperties;
using detail::copySchema;
std::uintptr_t g_main = 0;
template<class T> T read(const void* p, std::size_t offset) {
    T v; std::memcpy(&v, static_cast<const std::byte*>(p) + offset, sizeof(v)); return v;
}
template<class T> void write(void* p, std::size_t offset, T v) {
    std::memcpy(static_cast<std::byte*>(p) + offset, &v, sizeof(v));
}
template<class F> F native(std::uintptr_t offset) { return reinterpret_cast<F>(g_main + offset); }
struct Asset : detail::NativeEffectSchema {
    std::atomic<bool> ready{false};
    std::atomic<const void*> source{nullptr};
    std::atomic<const void*> instance{nullptr};
    const void* root = nullptr;
    alignas(8) std::byte user[112]{};
    float scale[3]{1, 1, 1};
    bool constructed = false;
    std::atomic<unsigned> renderedEvent{0};
    std::atomic<unsigned> renderedReports{0};
};
struct Slot {
    unsigned owner = kAssets;
    char name[128]{};
    pure::CompactEffectHandle source{};
    pure::CompactEffectHandle replay{};
    std::uint64_t seen = 0;
    bool visible = false;
    unsigned attempts = 0;
};
std::array<Asset, kAssets> g_assets;
std::array<Slot, pure::kEquipmentEffectLimit> g_slots;
pure::EffectMaskRestoration<kMasks> g_hidden;
std::array<std::atomic<const void*>, 32> g_live{};
std::atomic<std::uint64_t> g_selected{0};
std::atomic<const void*> g_creating{nullptr};
std::uintptr_t g_vtable[25]{};
std::atomic_flag g_guard = ATOMIC_FLAG_INIT;
struct Lock {
    Lock() { while (g_guard.test_and_set(std::memory_order_acquire)) {} }
    ~Lock() { g_guard.clear(std::memory_order_release); }
};
bool refuse(const char* why, unsigned value = 0) {
    static std::atomic<unsigned> count{0};
    const auto n = count.fetch_add(1) + 1;
    if (n <= 12 || n % 300 == 0)
        Logging.Log("[self-recall] EQUIPMENT_EFFECT_REFUSED reason=%s value=%u count=%u", why, value, n);
    return false;
}
const void* elink(const void* actor) {
    if (!actor) return nullptr;
    const auto* component = native<const void* (*)(const void*)>(kGetXLinkComponent)(actor);
    return component ? read<const void*>(component, 0x70) : nullptr;
}
bool valid(const pure::CompactEffectHandle& handle) {
    return handle.type == 0 && handle.poolIndex >= 0 &&
        native<bool (*)(const void*)>(kIsEventHandleValid)(&handle);
}
pure::CompactEffectHandle handleOf(const void* event) {
    const auto* bases = read<const std::uintptr_t*>(reinterpret_cast<const void*>(g_main), kEventPoolBasesSlot);
    const auto* strides = read<const std::uintptr_t*>(reinterpret_cast<const void*>(g_main), kEventPoolStridesSlot);
    if (!event || !bases || !strides || !strides[0]) return {};
    const auto address = reinterpret_cast<std::uintptr_t>(event);
    if (address < bases[0] || (address - bases[0]) % strides[0]) return {};
    const auto index = (address - bases[0]) / strides[0];
    if (index > 32767) return {};
    return {0, 0, static_cast<std::int16_t>(index), read<std::uint32_t>(event, 0x20)};
}

std::optional<unsigned> choosePrivateUserHash(void* system, const void* argument, unsigned hash) {
    const auto* creating = g_creating.load(std::memory_order_acquire);
    if (!creating || read<const void*>(argument, 8) != creating)
        return hash;
    const auto count = read<unsigned>(system, 0x20);
    const auto offset = read<std::int32_t>(system, 0x24);
    if (count > 4096 || offset < 0 || offset > 104) return std::nullopt;
    const auto* sentinel = static_cast<const std::byte*>(system) + 0x10;
    for (unsigned attempt = 0; attempt <= count; ++attempt, ++hash) {
        bool occupied = false;
        auto* node = read<const std::byte*>(system, 0x18);
        for (unsigned i = 0; node != sentinel && i < count; ++i) {
            if (!node) return std::nullopt;
            occupied |= read<unsigned>(node - offset, 0x40) == hash;
            node = read<const std::byte*>(node, 8);
        }
        if (!occupied) return hash;
    }
    return std::nullopt;
}

HOOK_DEFINE_TRAMPOLINE(PrivateUserHook) {
    static void* Callback(void* system, const void* argument, void* heap, unsigned hash) {
        const auto selected = choosePrivateUserHash(system, argument, hash);
        return selected ? Orig(system, argument, heap, *selected) : nullptr;
    }
};

struct BoneDescriptor {
    const void* root = nullptr;
    std::uint64_t indices = 0;
    ~BoneDescriptor() {}
};
static_assert(sizeof(BoneDescriptor) == 16);
__attribute__((noinline)) BoneDescriptor getBone(const void* user, const char* name) {
    for (auto& a : g_assets) {
        if (user != a.user || !a.root) continue;
        const auto count = read<unsigned>(a.root, 0x20);
        const auto* entries = read<const void* const*>(a.root, 0x28);
        if (!entries || count > pure::kPoseModelLimit) break;
        if (!name || !*name) return {a.root, 0};
        for (unsigned m = 0; m < count; ++m) {
            const auto* unit = entries[m] ? read<const void*>(entries[m], 0) : nullptr;
            if (!unit) continue;
            const auto* vtable = read<const void*>(unit, 0);
            const auto search = read<int (*)(const void*, const char* const*)>(vtable, 0x40);
            const auto bone = search(unit, &name);
            if (bone >= 0 && bone < 65535) return {a.root, m | (std::uint64_t(bone) << 16)};
        }
        break;
    }
    return {};
}

bool snapshotValues(Asset& a, const void* source) {
    const auto* scale = read<const float*>(source, 0x78);
    if (!scale) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(scale[i])) return false;
        a.scale[i] = scale[i];
    }
    const auto* values = read<const std::uint32_t*>(source, 0x98);
    const auto* pointers = read<const void* const*>(source, 0xA0);
    auto* target = const_cast<void*>(a.instance.load(std::memory_order_acquire));
    auto* output = target ? read<std::uint32_t*>(target, 0x98) : nullptr;
    if (a.propertyCount && (!values || !output)) return false;
    unsigned indirectCount = 0;
    for (unsigned i = 0; i < a.propertyCount; ++i)
        indirectCount += read<unsigned>(a.definitions[i], 0x58) >= 3;
    for (unsigned i = 0; i < a.propertyCount; ++i) {
        const auto type = read<unsigned>(a.definitions[i], 0x58);
        if (type <= 2) {
            a.values[i] = values[i];
            output[i] = values[i];
        } else {
            const auto index = values[i];
            if (index >= indirectCount || !pointers) return false;
            const auto* input = pointers[index];
            a.values[i] = input ? (type == 3 ? read<std::uint8_t>(input, 0) : read<std::uint32_t>(input, 0)) : 0;
            native<void (*)(void*, unsigned, const void*)>(kBindIndirectProperty)(target, i, &a.values[i]);
        }
    }
    return true;
}

void observe(void* executor) {
    const auto* source = read<const void*>(executor, 0x20);
    unsigned owner = kAssets;
    for (unsigned i = 0; i < kAssets; ++i)
        if (g_assets[i].ready.load(std::memory_order_acquire) && g_assets[i].source.load() == source) { owner = i; break; }
    if (owner == kAssets || !native<bool (*)(const void*)>(kIsLoopingExecutor)(executor)) return;
    const auto state = read<unsigned>(executor, 0x30);
    const auto* event = read<const void*>(executor, 0x18);
    if (!event || (state != 3 && state != 4) || (read<unsigned>(event, 8) & 0x30)) return;
    const auto* call = read<const void*>(event, 0x30);
    const auto* name = call ? read<const char*>(call, 0) : nullptr;
    if (!name) return;
    unsigned length = 0;
    while (length < 128 && name[length]) ++length;
    if (!length || length == 128) { refuse("event_name", length); return; }
    const auto handle = handleOf(event);
    if (handle.poolIndex < 0) return;
    Lock lock;
    auto& asset = g_assets[owner];
    if (!asset.ready.load() || asset.source.load() != source) return;
    Slot* empty = nullptr;
    for (auto& slot : g_slots) {
        if (slot.owner == owner && !std::strcmp(slot.name, name)) { empty = &slot; break; }
        if (slot.owner == kAssets && !empty) empty = &slot;
    }
    if (!empty) { refuse("event_capacity", pure::kEquipmentEffectLimit); return; }
    if (empty->owner == kAssets)
        Logging.Log("[self-recall] EQUIPMENT_LOOP_OBSERVED asset=%u user=%s cue=%s epoch=%llu",
            owner, asset.userName, name, static_cast<unsigned long long>(frame::diagnostics().frames));
    std::memcpy(empty->name, name, length + 1);
    empty->source = handle;
    empty->owner = owner;
    empty->seen = frame::diagnostics().frames;
    const auto* emitter = read<const void*>(executor, 0xB8);
    const auto id = read<unsigned>(executor, 0xC0);
    empty->visible = emitter && read<unsigned>(emitter, 0x234) == id && read<unsigned>(emitter, 0x34) != 0;
}

void restore(void* executor);
void observeAndHideEquipmentEffect(void* executor);

void synchronizeExistingLoops(const void* source) {
    const auto* user = read<const void*>(source, 0x58);
    const auto* resource = user ? read<const void*>(user, 0x18) : nullptr;
    const auto* system = resource ? read<const void*>(resource, 0x10) : nullptr;
    if (!system) return;
    const auto* table = read<const void*>(system, 0);
    auto* mutex = read<void* (*)(const void*)>(table, 0x38)(system);
    if (!mutex) return;
    const auto* mutexTable = read<const void*>(mutex, 0);
    read<void (*)(void*)>(mutexTable, 0x10)(mutex);
    const auto events = read<unsigned>(source, 0x38);
    const auto eventOffset = read<int>(source, 0x3C);
    const auto* sentinel = static_cast<const std::byte*>(source) + 0x28;
    auto* node = read<const std::byte*>(source, 0x30);
    if (events <= 256 && eventOffset == 0x10) {
        for (unsigned e = 0; e < events && node && node != sentinel; ++e) {
            const auto* event = node - eventOffset;
            const auto executors = read<unsigned>(event, 0xA8);
            const auto executorOffset = read<int>(event, 0xAC);
            auto* entry = read<const std::byte*>(event, 0xA0);
            const auto* end = event + 0x98;
            if (executors <= 256 && executorOffset == 8) {
                for (unsigned x = 0; x < executors && entry && entry != end; ++x) {
                    auto* executor = const_cast<std::byte*>(entry - executorOffset);
                    if (read<const void*>(executor, 0) == reinterpret_cast<const void*>(g_main + kEffectExecutorVtable)) {
                        restore(executor);
                        observeAndHideEquipmentEffect(executor);
                    }
                    entry = read<const std::byte*>(entry, 8);
                }
            } else { refuse("executor_list", executors); }
            node = read<const std::byte*>(node, 8);
        }
    } else { refuse("event_list", events); }
    read<void (*)(void*)>(mutexTable, 0x18)(mutex);
}

bool hide(const void* source) {
    if (!pose_session::active()) return false;
    for (const auto& live : g_live) if (live.load(std::memory_order_acquire) == source) return true;
    for (unsigned a = 0; a < kAssets; ++a) {
        if (g_assets[a].instance.load(std::memory_order_acquire) != source) continue;
        return g_selected.load(std::memory_order_acquire) == 0;
    }
    return false;
}
void restore(void* executor) {
    auto* emitter = read<void*>(executor, 0xB8);
    const auto id = read<std::uint32_t>(executor, 0xC0);
    Lock lock;
    const auto saved = g_hidden.restore({reinterpret_cast<std::uintptr_t>(executor),
                                        reinterpret_cast<std::uintptr_t>(emitter), id});
    if (saved && emitter && read<unsigned>(emitter, 0x234) == id) write(emitter, 0x34, *saved);
}
void observeAndHideEquipmentEffect(void* executor) {
    observe(executor);
    const auto* user = read<const void*>(executor, 0x20);
    for (unsigned i = 0; i < kAssets; ++i) {
        auto& asset = g_assets[i];
        if (!user || asset.instance.load(std::memory_order_acquire) != user || !asset.ready.load()) continue;
        const auto* event = read<const void*>(executor, 0x18);
        const auto id = event ? read<unsigned>(event, 0x20) : 0;
        if (!id || asset.renderedEvent.exchange(id) == id) break;
        const auto reports = asset.renderedReports.fetch_add(1) + 1;
        if (reports > 3 && reports % 300 != 0) break;
        const auto* emitter = read<const void*>(executor, 0xB8);
        const bool emitterValid = emitter && read<unsigned>(emitter, 0x234) == read<unsigned>(executor, 0xC0);
        Logging.Log("[self-recall] EQUIPMENT_LOOP_RENDER asset=%u event=%u state=%u emitter=%u mask=%x",
            i, id, read<unsigned>(executor, 0x30), unsigned(emitterValid),
            emitterValid ? read<unsigned>(emitter, 0x34) : 0);
        break;
    }
    if (!user || !hide(user)) return;
    auto* emitter = read<void*>(executor, 0xB8);
    const auto id = read<std::uint32_t>(executor, 0xC0);
    if (!emitter || read<unsigned>(emitter, 0x234) != id) return;
    Lock lock;
    if (g_hidden.remember({reinterpret_cast<std::uintptr_t>(executor),
                          reinterpret_cast<std::uintptr_t>(emitter), id}, read<unsigned>(emitter, 0x34))) {
        write<unsigned>(emitter, 0x34, 0);
        return;
    }
    refuse("mask_capacity", kMasks);
    return;
}

HOOK_DEFINE_TRAMPOLINE(CalcHook) {
    static std::uint64_t Callback(void* executor) {
        restore(executor);
        const auto result = Orig(executor);
        observeAndHideEquipmentEffect(executor);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(ExecutorDestroyedHook) {
    static std::uintptr_t Callback(void* executor) {
        restore(executor);
        return Orig(executor);
    }
};
}

void install(std::uintptr_t mainBase) {
    g_main = mainBase;
    std::memcpy(g_vtable, reinterpret_cast<const void*>(g_main + kPreActorUserVtable), sizeof(g_vtable));
    g_vtable[4] = reinterpret_cast<std::uintptr_t>(&getBone);
    PrivateUserHook::InstallAtOffset(kCreateUser);
    CalcHook::InstallAtOffset(kCalculateEffect);
    ExecutorDestroyedHook::InstallAtOffset(kDestroyExecutor);
}

bool retain(unsigned asset, const void* actor, const void* copiedRoot) {
    if (asset >= kAssets || !copiedRoot) return false;
    auto& a = g_assets[asset];
    const auto* source = elink(actor);
    if (!source) { a.source.store(nullptr); return true; }
    for (unsigned i = 0; i < kAssets; ++i) if (i != asset) {
        const void* previous = source;
        g_assets[i].source.compare_exchange_strong(previous, nullptr, std::memory_order_acq_rel);
    }
    if (a.ready.load(std::memory_order_acquire)) {
        const auto* schema = read<const void*>(source, 0x58);
        const auto* name = schema ? read<const char*>(schema, 0x10) : nullptr;
        if (!name || std::strcmp(name, a.userName) || read<std::uint16_t>(schema, 0x44) != a.propertyCount)
            return refuse("schema_changed", asset);
        a.source.store(source, std::memory_order_release);
        if (!snapshotValues(a, source)) return refuse("property_values", asset);
        synchronizeExistingLoops(source);
        return true;
    }
    if (equipment::contiguousArchiveBytes() < 256 * 1024 || !copySchema(a, source))
        return refuse("schema", asset);
    a.root = copiedRoot;
    native<void (*)(void*)>(kConstructPreActorUser)(a.user);
    a.constructed = true;
    write<const void*>(a.user, 0, g_vtable);
    const char* empty = "";
    g_creating.store(a.user, std::memory_order_release);
    native<void (*)(void*, const char* const*, const char* const*, void*, void*)>(kInitializePreActorUser)(
        a.user, &a.userName, &empty, a.table, equipment::archiveHeap());
    g_creating.store(nullptr, std::memory_order_release);
    const auto* instance = read<const void*>(a.user, 0x18);
    a.instance.store(instance, std::memory_order_release);
    if (!instance) { retire(asset); return refuse("create", asset); }
    const auto initialFlags = read<unsigned>(instance, 0x18);
    native<void (*)(void*)>(kResetPreActorUser)(a.user);
    if (!snapshotValues(a, source)) { retire(asset); return refuse("property_values", asset); }
    struct MatrixArgument { const void* root; std::uint64_t indices, unused; const float* scale; };
    const MatrixArgument matrix{copiedRoot, 0, 0, a.scale};
    native<void (*)(const void*, const void*)>(kSetBoneMatrix)(instance, &matrix);
    a.source.store(source, std::memory_order_release);
    a.ready.store(true, std::memory_order_release);
    synchronizeExistingLoops(source);
    Logging.Log("[self-recall] EQUIPMENT_EFFECT_ARCHIVED asset=%u user=%s properties=%u flags=%x->%x", asset, a.userName, a.propertyCount,
        initialFlags, read<unsigned>(instance, 0x18));
    return true;
}

void retire(unsigned asset) {
    if (asset >= kAssets) return;
    auto& a = g_assets[asset];
    a.ready.store(false, std::memory_order_release);
    a.source.store(nullptr, std::memory_order_release);
    if (a.constructed) {
        native<void (*)(void*, void*)>(kFinalizePreActorUser)(a.user, nullptr);
        native<void (*)(void*)>(kDestroyPreActorUser)(a.user);
        a.constructed = false;
    }
    a.instance.store(nullptr, std::memory_order_release);
    a.renderedEvent.store(0);
    a.renderedReports.store(0);
    a.root = nullptr;
    Lock lock;
    for (auto& slot : g_slots) if (slot.owner == asset) slot = {};
}

void record(unsigned asset, pure::PoseFrameHeader& header) {
    if (asset >= kAssets || !g_assets[asset].ready.load()) return;
    Lock lock;
    auto mask = pure::equipmentEffectMask(header);
    for (unsigned i = 0; i < g_slots.size(); ++i)
        if (g_slots[i].owner == asset && g_slots[i].visible &&
            header.frameEpoch >= g_slots[i].seen &&
            valid(g_slots[i].source)) mask |= std::uint64_t{1} << i;
    pure::setEquipmentEffectMask(header, mask);
}

void selectFrame(const pure::RecordedPoseFrame* frame) {
    const auto mask = frame ? pure::equipmentEffectMask(frame->header) : 0;
    const auto previous = g_selected.exchange(mask, std::memory_order_acq_rel);
    if (previous != mask)
        Logging.Log("[self-recall] EQUIPMENT_LOOP_SELECTION mask=%llx key=%llu",
            static_cast<unsigned long long>(mask),
            static_cast<unsigned long long>(frame ? frame->header.key.serial : 0));
    for (unsigned i = 0; i < g_slots.size(); ++i) {
        unsigned owner;
        char name[128];
        pure::CompactEffectHandle replay;
        {
            Lock lock;
            owner = g_slots[i].owner;
            if (owner == kAssets) continue;
            replay = g_slots[i].replay;
            std::memcpy(name, g_slots[i].name, sizeof(name));
        }
        auto& asset = g_assets[owner];
        auto* user = const_cast<void*>(asset.instance.load(std::memory_order_acquire));
        if (!user || !asset.ready.load()) continue;
        const auto flags = read<unsigned>(user, 0x18);
        const auto action = pure::planEquipmentLoop(mask & (std::uint64_t{1} << i),
                                                     valid(replay), (flags & 2u) != 0);
        if (action == pure::EquipmentLoopAction::Kill) {
            native<void (*)(void*)>(kKillEvent)(&replay);
            replay = {};
        } else if (action == pure::EquipmentLoopAction::Emit ||
                   action == pure::EquipmentLoopAction::WakeAndEmit) {
            if (action == pure::EquipmentLoopAction::WakeAndEmit) {
                native<void (*)(void*)>(kResetPreActorUser)(asset.user);
                Logging.Log("[self-recall] EQUIPMENT_EFFECT_WAKE asset=%u flags=%x->%x",
                    owner, flags, read<unsigned>(user, 0x18));
            }
            replay = {};
            native<void (*)(void*, const char*, void*)>(kEmitEvent)(user, name, &replay);
            unsigned attempt;
            { Lock lock; attempt = ++g_slots[i].attempts; }
            if (valid(replay) || attempt <= 3 || (attempt & (attempt - 1)) == 0) {
                const auto currentFlags = read<unsigned>(user, 0x18);
                const auto* setup = read<const void*>(user, 0x48 + 8 * (currentFlags & 1u));
                const auto* definition = read<const void*>(user, 0x58);
                const auto* resource = definition ? read<const void*>(definition, 0x18) : nullptr;
                const auto choice = resource ? read<unsigned>(resource, 0x18) : 0;
                const auto* parameters = resource ? read<const void*>(resource, 0x20 + 8 * (choice < 2 ? choice : 0)) : nullptr;
                const auto* header = parameters ? read<const void*>(parameters, 8) : nullptr;
                Logging.Log("[self-recall] EQUIPMENT_LOOP_REPLAY asset=%u cue=%s valid=%u key=%llu flags=%x attempt=%u setup=%u calls=%u",
                    owner, name, unsigned(valid(replay)),
                    static_cast<unsigned long long>(frame->header.key.serial), currentFlags, attempt,
                    setup ? read<unsigned char>(setup, 0x20) : 0, header ? read<unsigned>(header, 8) : 0);
            }
        }
        Lock lock;
        if (g_slots[i].owner == owner) g_slots[i].replay = replay;
    }
    for (auto& asset : g_assets) {
        auto* user = const_cast<void*>(asset.instance.load(std::memory_order_acquire));
        if (user && asset.ready.load()) native<void (*)(void*)>(kRequestUserCalculation)(user);
    }
}

void publishLive(std::span<const void* const> actors) {
    for (unsigned i = 0; i < g_live.size(); ++i)
        g_live[i].store(i < actors.size() ? elink(actors[i]) : nullptr, std::memory_order_release);
    for (const auto* actor : actors)
        if (const auto* source = elink(actor)) synchronizeExistingLoops(source);
}

void captureValues(unsigned asset, Values& out) {
    out = {};
    if (asset >= kAssets || !g_assets[asset].ready.load(std::memory_order_acquire)) return;
    const auto& a = g_assets[asset];
    out.count = a.propertyCount;
    std::memcpy(out.scale, a.scale, sizeof(out.scale));
    std::memcpy(out.properties, a.values, a.propertyCount * sizeof(std::uint32_t));
}

bool applyValues(unsigned asset, const Values& values) {
    if (asset >= kAssets) return false;
    auto& a = g_assets[asset];
    if (!a.ready.load(std::memory_order_acquire)) return values.count == 0;
    if (!values.count && a.propertyCount) return true;
    auto* instance = const_cast<void*>(a.instance.load(std::memory_order_acquire));
    if (!instance || values.count != a.propertyCount) return refuse("historical_properties", asset);
    auto* output = read<std::uint32_t*>(instance, 0x98);
    if (values.count && !output) return false;
    std::memcpy(a.scale, values.scale, sizeof(a.scale));
    std::memcpy(a.values, values.properties, values.count * sizeof(std::uint32_t));
    for (unsigned i = 0; i < values.count; ++i)
        if (read<unsigned>(a.definitions[i], 0x58) <= 2) output[i] = a.values[i];
    return true;
}

bool copyMatrix(const void* descriptor, float out[12]) {
    if (!descriptor || !pose_session::active()) return false;
    const auto* root = read<const void*>(descriptor, 0);
    const auto indices = read<std::uint64_t>(descriptor, 8);
    const auto m = static_cast<std::uint16_t>(indices), bone = static_cast<std::uint16_t>(indices >> 16);
    for (const auto& a : g_assets) {
        if (!a.ready.load(std::memory_order_acquire) || root != a.root) continue;
        if (m >= read<unsigned>(root, 0x20)) return false;
        const auto* entries = read<const void* const*>(root, 0x28);
        const auto* unit = entries && entries[m] ? read<const void*>(entries[m], 0) : nullptr;
        return unit && pose_render::copyBone(unit, bone, out);
    }
    return false;
}
} // namespace self_recall::equipment_effects
