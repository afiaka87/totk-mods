#include "totk/engine/ReadGuard.hpp"
#include "RecallRuntimeEngine.hpp"
#include "GameProfiles.hpp"
#include "RecallEffectsEngine.hpp"
#include "RecallModelEngine.hpp"
#include "RecallRender.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <optional>
#include <lib.hpp>

namespace self_recall::equipment_effects {
namespace {
struct EffectProfile {
    std::uintptr_t getXLinkComponent, actorGetModel, isEventHandleValid;
    std::uintptr_t eventPoolBasesSlot, eventPoolStridesSlot;
    std::uintptr_t bindIndirectProperty, isLoopingExecutor, preActorUserVtable;
    std::uintptr_t createUser, calculateEffect, destroyExecutor;
    std::uintptr_t constructPreActorUser, initializePreActorUser, resetPreActorUser;
    std::uintptr_t effectExecutorVtable, setBoneMatrix, finalizePreActorUser;
    std::uintptr_t destroyPreActorUser, killEvent, emitEvent, requestUserCalculation;
};

constexpr EffectProfile kProfiles[] = {
    {0x1203CEC, 0xAF74D8, 0xCB9B78, 0x4558A98, 0x4558AA0,
      0xD92B28, 0x22172BC, 0x45387B8, 0xD93250, 0x72F34C, 0x29EC884,
      0x15BD77C, 0xCFED6C, 0xCFE904, 0x44F7C28, 0x2221254,
      0x87BD20, 0x243E7D4, 0xB5C2F8, 0xCB99A4, 0xE8217C},
    {0x10F0D34, 0xB6D154, 0xD1B63C, 0x4631B80, 0x4631B88,
      0xDB6244, 0x2298BB8, 0x46116D8, 0xDB694C, 0x7AB068, 0x2A64EC4,
      0x1600870, 0xD6BE74, 0xD6BA08, 0x45D0518, 0x22A2D5C,
      0x8A4430, 0x24C491C, 0xBB332C, 0xD1B3A4, 0xEC445C},
    {0x10E7F14, 0xB5FAD4, 0xD8A388, 0x462BEE8, 0x462BEF0,
      0xD90DAC, 0x22902A0, 0x460BA48, 0xD914B4, 0x7B0B80, 0x2A5C044,
      0x15F7780, 0xD4AD44, 0xD4A8D8, 0x45CA888, 0x229A79C,
      0x8B368C, 0x24BC088, 0xB90BD4, 0xD8A284, 0xEB21C8},
    {0xF615F8, 0xB98068, 0xDAE878, 0x46202B8, 0x46202C0,
      0xDA20E8, 0x2286C90, 0x45FFDF8, 0xDA27F0, 0x78713C, 0x2A4FE04,
      0x15E4514, 0xD3DE74, 0xD3DA08, 0x45BEC38, 0x22902FC,
      0x8802B8, 0x24AF37C, 0x8AFBC0, 0xDAE774, 0xEEA0F0},
    {0x1066DA0, 0xBADE58, 0xD17C80, 0x462F290, 0x462F298,
      0xD76F94, 0x2290B00, 0x460EDE0, 0xD7769C, 0x7C7104, 0x2A5F1E4,
      0x15F154C, 0xD1C7F4, 0xD1C388, 0x45CDC38, 0x229A2FC,
      0x8CB400, 0x24BEEEC, 0xBBCCAC, 0xD179E8, 0xEA4958},
    {0x2AE9458, 0xB93318, 0xD38D4, 0x3950388, 0x3950390,
      0x2651D4, 0x28D1BC4, 0x3947CA0, 0x264520, 0x3A676C, 0x28D3824,
      0x2B6CDA8, 0x35C708, 0x2B6CFB0, 0x3908A90, 0x28E09AC,
      0x2B6CE44, 0x2B6CDF8, 0xCA03D8, 0xA9636C, 0x44F604},
    {0x2AE2B78, 0xB8DE58, 0x54564C, 0x394B388, 0x394B390,
      0x2961E4, 0x28C9924, 0x3942CA0, 0x295520, 0x36B708, 0x28CB584,
      0x2B67A58, 0x869A80, 0x2B67C60, 0x3903A90, 0x28D8A30,
      0x2B67AF4, 0x2B67AA8, 0xC8B170, 0xA89848, 0x3519D4},
    {0x2AE313C, 0xB89A90, 0x55F048, 0x394D388, 0x394D390,
      0x2596E4, 0x28C96C0, 0x3944CA0, 0x258A30, 0x1CB280, 0x28CB320,
      0x2B683C8, 0x29C2B8, 0x2B685D0, 0x3905A90, 0x28D8890,
      0x2B68464, 0x2B68418, 0xC985E8, 0xAC1D50, 0x369230},
    {0x2AF5338, 0xBA8854, 0x481918, 0x395F388, 0x395F390,
      0x36E224, 0x28D9B68, 0x3956CA0, 0x36DCF0, 0x395500, 0x28DB7C8,
      0x2B7BBE8, 0x889858, 0x2B7BDF0, 0x3917A90, 0x28E8BC8,
      0x2B7BC84, 0x2B7BC38, 0xC89FB4, 0xA98CF8, 0x340AD8},
};

const EffectProfile* g_profile = nullptr;
void selectProfile() {
    g_profile = &profiles::row(kProfiles);
}
}
}

#if SELF_RECALL_STORAGE_PROFILE == 8

namespace self_recall::equipment_effects {
namespace {
constexpr unsigned kAssets = 64, kMasks = 256;
using detail::kProperties;
using detail::copySchema;
std::uintptr_t g_main = 0;
template<class T> T read(const void* p, std::size_t offset) {
    if (!totk::engine::read_guard::admit(p, offset, sizeof(T))) return T{};
    T v; std::memcpy(&v, static_cast<const std::byte*>(p) + offset, sizeof(v)); return v;
}
template<class T> void write(void* p, std::size_t offset, T v) {
    std::memcpy(static_cast<std::byte*>(p) + offset, &v, sizeof(v));
}
template<class F> F native(std::uintptr_t offset) { return reinterpret_cast<F>(g_main + offset); }
using Schema = detail::PackedEffectSchema;
struct Asset : Schema {
    std::atomic<bool> ready{false};
    std::atomic<const void*> source{nullptr};
    std::atomic<const void*> instance{nullptr};
    const void* root = nullptr;
    alignas(8) std::byte user[112]{};
    float scale[3]{1, 1, 1};
    bool constructed = false;
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
    const auto* component = native<const void* (*)(const void*)>(g_profile->getXLinkComponent)(actor);
    return component ? read<const void*>(component, 0x70) : nullptr;
}
bool valid(const pure::CompactEffectHandle& handle) {
    return handle.type == 0 && handle.poolIndex >= 0 &&
        native<bool (*)(const void*)>(g_profile->isEventHandleValid)(&handle);
}
pure::CompactEffectHandle handleOf(const void* event) {
    const auto* bases = read<const std::uintptr_t*>(reinterpret_cast<const void*>(g_main), g_profile->eventPoolBasesSlot);
    const auto* strides = read<const std::uintptr_t*>(reinterpret_cast<const void*>(g_main), g_profile->eventPoolStridesSlot);
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
            native<void (*)(void*, unsigned, const void*)>(g_profile->bindIndirectProperty)(target, i, &a.values[i]);
        }
    }
    return true;
}

void observe(void* executor) {
    const auto* source = read<const void*>(executor, 0x20);
    unsigned owner = kAssets;
    for (unsigned i = 0; i < kAssets; ++i) {
        if (g_assets[i].ready.load(std::memory_order_acquire) &&
            g_assets[i].source.load() == source) {
            owner = i;
            break;
        }
    }
    if (owner == kAssets || !native<bool (*)(const void*)>(g_profile->isLoopingExecutor)(executor)) return;
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
    if (!g_assets[owner].ready.load() || g_assets[owner].source.load() != source) return;
    Slot* empty = nullptr;
    for (auto& slot : g_slots) {
        if (slot.owner == owner && !std::strcmp(slot.name, name)) { empty = &slot; break; }
        if (slot.owner == kAssets && !empty) empty = &slot;
    }
    if (!empty) { refuse("event_capacity", pure::kEquipmentEffectLimit); return; }
    std::memcpy(empty->name, name, length + 1);
    empty->source = handle;
    empty->owner = owner;
    empty->seen = frame::epoch();
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
                    if (read<const void*>(executor, 0) ==
                        reinterpret_cast<const void*>(
                            g_main + g_profile->effectExecutorVtable)) {
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
    selectProfile();
    if (!g_profile) return;
    std::memcpy(g_vtable, reinterpret_cast<const void*>(g_main + g_profile->preActorUserVtable), sizeof(g_vtable));
    g_vtable[4] = reinterpret_cast<std::uintptr_t>(&getBone);
    PrivateUserHook::InstallAtOffset(g_profile->createUser);
    CalcHook::InstallAtOffset(g_profile->calculateEffect);
    ExecutorDestroyedHook::InstallAtOffset(g_profile->destroyExecutor);
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
    if (equipment::contiguousArchiveBytes() < 256 * 1024)
        return refuse("schema", asset);
    const auto size = detail::measureSchema(source);
    auto* bytes = size ? static_cast<std::byte*>(
                             equipment::allocateArchive(size.bytes()))
                       : nullptr;
    if (!bytes) return refuse("schema_copy", asset);
    if (!a.bind({bytes, size.bytes()}, size) || !copySchema(a, source)) {
        equipment::freeArchive(bytes);
        static_cast<detail::PackedEffectSchema&>(a) = {};
        return refuse("schema_copy", asset);
    }
    a.root = copiedRoot;
    native<void (*)(void*)>(g_profile->constructPreActorUser)(a.user);
    a.constructed = true;
    write<const void*>(a.user, 0, g_vtable);
    const char* empty = "";
    g_creating.store(a.user, std::memory_order_release);
    native<void (*)(void*, const char* const*, const char* const*, void*, void*)>(g_profile->initializePreActorUser)(
        a.user, &a.userName, &empty, a.table, equipment::archiveHeap());
    g_creating.store(nullptr, std::memory_order_release);
    const auto* instance = read<const void*>(a.user, 0x18);
    a.instance.store(instance, std::memory_order_release);
    if (!instance) { retire(asset); return refuse("create", asset); }
    native<void (*)(void*)>(g_profile->resetPreActorUser)(a.user);
    if (!snapshotValues(a, source)) { retire(asset); return refuse("property_values", asset); }
    struct MatrixArgument { const void* root; std::uint64_t indices, unused; const float* scale; };
    const MatrixArgument matrix{copiedRoot, 0, 0, a.scale};
    native<void (*)(const void*, const void*)>(g_profile->setBoneMatrix)(instance, &matrix);
    a.source.store(source, std::memory_order_release);
    a.ready.store(true, std::memory_order_release);
    synchronizeExistingLoops(source);
    return true;
}

void retire(unsigned asset) {
    if (asset >= kAssets) return;
    auto& a = g_assets[asset];
    a.ready.store(false, std::memory_order_release);
    a.source.store(nullptr, std::memory_order_release);
    if (a.constructed) {
        native<void (*)(void*, void*)>(g_profile->finalizePreActorUser)(a.user, nullptr);
        native<void (*)(void*)>(g_profile->destroyPreActorUser)(a.user);
        a.constructed = false;
    }
    a.instance.store(nullptr, std::memory_order_release);
    a.root = nullptr;
    Lock lock;
    for (auto& slot : g_slots) if (slot.owner == asset) slot = {};
    equipment::freeArchive(a.storage);
    static_cast<detail::PackedEffectSchema&>(a) = {};
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
    g_selected.store(mask, std::memory_order_release);
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
            native<void (*)(void*)>(g_profile->killEvent)(&replay);
            replay = {};
        } else if (action == pure::EquipmentLoopAction::Emit ||
                   action == pure::EquipmentLoopAction::WakeAndEmit) {
            if (action == pure::EquipmentLoopAction::WakeAndEmit) {
                native<void (*)(void*)>(g_profile->resetPreActorUser)(asset.user);
            }
            replay = {};
            native<void (*)(void*, const char*, void*)>(g_profile->emitEvent)(user, name, &replay);
            if (!valid(replay)) {
                unsigned attempt;
                {
                    Lock lock;
                    attempt = ++g_slots[i].attempts;
                }
                if (attempt <= 3 || (attempt & (attempt - 1)) == 0)
                    Logging.Log(
                        "[self-recall] EQUIPMENT_LOOP_REPLAY_FAILED asset=%u "
                        "cue=%s key=%llu attempt=%u",
                        owner, name,
                        static_cast<unsigned long long>(
                            frame ? frame->header.key.serial : 0),
                        attempt);
            }
        }
        Lock lock;
        if (g_slots[i].owner == owner) g_slots[i].replay = replay;
    }
    for (auto& asset : g_assets) {
        auto* user = const_cast<void*>(asset.instance.load(std::memory_order_acquire));
        if (user && asset.ready.load()) native<void (*)(void*)>(g_profile->requestUserCalculation)(user);
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

EffectMatrix copyMatrix(const void*, const void* descriptor, float out[12], const void**, unsigned*) {
    if (!descriptor || !pose_session::active()) return EffectMatrix::Foreign;
    const auto* root = read<const void*>(descriptor, 0);
    const auto indices = read<std::uint64_t>(descriptor, 8);
    const auto m = static_cast<std::uint16_t>(indices), bone = static_cast<std::uint16_t>(indices >> 16);
    for (const auto& a : g_assets) {
        if (!a.ready.load(std::memory_order_acquire) || root != a.root) continue;
        if (m >= read<unsigned>(root, 0x20)) return EffectMatrix::Foreign;
        const auto* entries = read<const void* const*>(root, 0x28);
        const auto* unit = entries && entries[m] ? read<const void*>(entries[m], 0) : nullptr;
        return unit && pose_render::copyBone(unit, bone, out) ? EffectMatrix::Bone : EffectMatrix::Foreign;
    }
    return EffectMatrix::Foreign;
}
}

#else

namespace self_recall::equipment_effects {
namespace {
std::uintptr_t g_main = 0;
template<class T> T read(const void* p, std::size_t offset) {
    if (!totk::engine::read_guard::admit(p, offset, sizeof(T))) return T{};
    T v; std::memcpy(&v, static_cast<const std::byte*>(p) + offset, sizeof(v)); return v;
}
// Switch keeps native gear effects: maps an effect user to its nameable model roots and unboned-cue model.
struct LiveGear {
    std::atomic<const void*> user{nullptr}, root{nullptr}, nativeRoot{nullptr}, unit{nullptr};
};
std::array<LiveGear, model::kOwnedActorLimit> g_gear;

const void* firstUnit(const void* root) {
    if (!root || read<std::int32_t>(root, 0x20) <= 0) return nullptr;
    const auto* entries = read<const void* const*>(root, 0x28);
    return entries && entries[0] ? read<const void*>(entries[0], 0) : nullptr;
}
}

void install(std::uintptr_t mainBase) { g_main = mainBase; selectProfile(); }

void publishLive(std::span<const void* const> actors) {
    if (!g_main || !g_profile) return;
    for (unsigned i = 0; i < g_gear.size(); ++i) {
        const auto* actor = i < actors.size() ? actors[i] : nullptr;
        const auto* component = actor
            ? reinterpret_cast<const void* (*)(const void*)>(g_main + g_profile->getXLinkComponent)(actor) : nullptr;
        const auto* user = component ? read<const void*>(component, 0x70) : nullptr;
        const auto* root = user ? actor_model::actorModel(actor) : nullptr;
        const auto* nativeRoot = user
            ? reinterpret_cast<const void* (*)(const void*)>(g_main + g_profile->actorGetModel)(actor) : nullptr;
        auto& gear = g_gear[i];
        gear.user.store(nullptr, std::memory_order_release);
        gear.root.store(root, std::memory_order_release);
        gear.nativeRoot.store(nativeRoot, std::memory_order_release);
        gear.unit.store(firstUnit(root), std::memory_order_release);
        gear.user.store(user, std::memory_order_release);
    }
}

EffectMatrix copyMatrix(const void* executor, const void* descriptor, float out[12],
                        const void** anchorUnit, unsigned* anchorBone) {
    if (!executor || !descriptor || !pose_session::active()) return EffectMatrix::Foreign;
    const auto* user = read<const void*>(executor, 0x20);
    if (!user) return EffectMatrix::Foreign;
    const LiveGear* owner = nullptr;
    for (const auto& gear : g_gear)
        if (gear.user.load(std::memory_order_acquire) == user) { owner = &gear; break; }
    if (!owner) return EffectMatrix::Foreign;
    const auto* root = read<const void*>(descriptor, 0);
    for (const auto& gear : g_gear) {
        if (!root || !gear.user.load(std::memory_order_acquire) ||
            (root != gear.root.load(std::memory_order_acquire) &&
             root != gear.nativeRoot.load(std::memory_order_acquire))) continue;
        const auto indices = read<std::uint64_t>(descriptor, 8);
        const auto m = static_cast<std::uint16_t>(indices);
        const auto bone = static_cast<std::uint16_t>(indices >> 16);
        const auto count = read<std::int32_t>(root, 0x20);
        const auto* entries = read<const void* const*>(root, 0x28);
        const auto* unit = entries && m < count && entries[m] ? read<const void*>(entries[m], 0) : nullptr;
        if (!unit || !pose_render::copyBone(unit, bone, out)) return EffectMatrix::Missing;
        *anchorUnit = unit;
        *anchorBone = bone;
        return EffectMatrix::Bone;
    }
    // Unboned cues such as the Master Sword glow follow the owner's first model root bone.
    const auto* unit = owner->unit.load(std::memory_order_acquire);
    if (!unit || !pose_render::copyBone(unit, 0, out)) return EffectMatrix::Missing;
    *anchorUnit = unit;
    *anchorBone = 0;
    return EffectMatrix::ModelRoot;
}
}

#endif
