#include "totk/engine/ReadGuard.hpp"
#include "RecallGraphicsEngine.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <heap/seadHeap.h>
#include <lib.hpp>

#include "RecallBase.hpp"
#include "RecallModelEngine.hpp"
#include "GameProfiles.hpp"

namespace self_recall::native_path {
namespace {
std::uintptr_t g_mainBase = 0, g_nativeOwner = 0;
std::byte* g_renderer = nullptr;
sead::Heap* g_heap = nullptr;
pure::FrameMailbox<pure::NativePathRoute> g_publication;
pure::NativePathRoute g_route;
pure::NativePathFrame g_path;
std::uint64_t g_epoch = 0, g_preparedEpoch = 0, g_maskedEpoch = 0;
std::uint32_t g_generation = 0;
std::atomic<std::uint64_t> g_failure{0};
std::uint32_t g_packedCount = 0;
bool g_maskInProgress = false;
bool g_ownerFault = false;


struct RendererLayout {
    std::size_t bytes, selected, processed, mode, color0, color1, depth;
    std::size_t endpoint, secondaryCount, debugFlags, resource;
};
constexpr RendererLayout kOlderLayout{11216, 10848, 10968, 372, 0x158, 0x160, 0x168,
                                      11116, 11136, 11202, 336};
constexpr RendererLayout kNewerLayout{11224, 10856, 10976, 380, 0x160, 0x168, 0x170,
                                      11124, 11144, 11210, 344};

struct NativePathProfile {
    profiles::Version version;
    std::ptrdiff_t allocatorSlot, textureFree, nodeClear, nodeAdd, nodeReverse;
    std::ptrdiff_t selectedDraw, constructor, initializer, vtable;
    profiles::Site calcView, pathPack, auxiliary, mask, composite, destroy;
    std::ptrdiff_t maskBody;
    const RendererLayout* layout;
    bool compositeNeedsDepth;
};

constexpr std::array<NativePathProfile, 9> kProfiles{{
    {
        .version = profiles::Version::V100,
        .allocatorSlot = 0x4561A40, .textureFree = 0x7981B4,
        .nodeClear = 0x171C544, .nodeAdd = 0x171E4D4, .nodeReverse = 0x171CA78,
        .selectedDraw = 0xBE36CC, .constructor = 0xC57C08,
        .initializer = 0xC47818, .vtable = 0x42A7760,
        .calcView = {0xAA154C, 0xD10283FF}, .pathPack = {0x18723E4, 0xD10243FF},
        .auxiliary = {0xBE3870, 0xA9BA7BFD}, .mask = {0xBE2334, 0xFC190FE8},
        .composite = {0xBE18B0, 0xA9BA7BFD}, .destroy = {0x18721A0, 0xA9BC7BFD},
        .maskBody = 0,
        .layout = &kOlderLayout, .compositeNeedsDepth = true,
    },
    {
        .version = profiles::Version::V110,
        .allocatorSlot = 0x463AAE8, .textureFree = 0x830100,
        .nodeClear = 0x1767868, .nodeAdd = 0x1769A98, .nodeReverse = 0x1767E44,
        .selectedDraw = 0x96095C, .constructor = 0xCC55F8,
        .initializer = 0xCB44F4, .vtable = 0x43757F0,
        .calcView = {0x96DDAC, 0xD10283FF}, .pathPack = {0x18C0224, 0xD10243FF},
        .auxiliary = {0x960B00, 0xA9BA7BFD}, .mask = {0x95EBE4, 0xA9BA7BFD},
        .composite = {0x95E14C, 0xA9BA7BFD}, .destroy = {0x18BFFE0, 0xA9BC7BFD},
        .maskBody = 0,
        .layout = &kOlderLayout, .compositeNeedsDepth = false,
    },
    {
        .version = profiles::Version::V112,
        .allocatorSlot = 0x4634E70, .textureFree = 0x7F0354,
        .nodeClear = 0x175C108, .nodeAdd = 0x175E338, .nodeReverse = 0x175C6E4,
        .selectedDraw = 0x932474, .constructor = 0xC8F2B4,
        .initializer = 0xC7E1A4, .vtable = 0x436F7F8,
        .calcView = {0x93C798, 0xD10283FF}, .pathPack = {0x18B5144, 0xD10243FF},
        .auxiliary = {0x932618, 0xA9BA7BFD}, .mask = {0x9306F4, 0xA9BA7BFD},
        .composite = {0x92FC5C, 0xA9BA7BFD}, .destroy = {0x18B4F00, 0xA9BC7BFD},
        .maskBody = 0,
        .layout = &kOlderLayout, .compositeNeedsDepth = false,
    },
    {
        .version = profiles::Version::V120,
        .allocatorSlot = 0x4629218, .textureFree = 0x79A29C,
        .nodeClear = 0x17486EC, .nodeAdd = 0x174A91C, .nodeReverse = 0x1748CC8,
        .selectedDraw = 0xB63CA8, .constructor = 0xC5A9EC,
        .initializer = 0xC498D4, .vtable = 0x43637F8,
        .calcView = {0xAE9498, 0xD10283FF}, .pathPack = {0x18A18A4, 0xD10243FF},
        .auxiliary = {0xB63E4C, 0xA9BA7BFD}, .mask = {0xB62AB8, 0xA9BA7BFD},
        .composite = {0xB62020, 0xA9BA7BFD}, .destroy = {0x18A1660, 0xA9BC7BFD},
        .maskBody = 0,
        .layout = &kOlderLayout, .compositeNeedsDepth = false,
    },
    {
        .version = profiles::Version::V121,
        .allocatorSlot = 0x4638208, .textureFree = 0x7EF80C,
        .nodeClear = 0x1757E08, .nodeAdd = 0x175A038, .nodeReverse = 0x17583E4,
        .selectedDraw = 0xC334E4, .constructor = 0xC96E04,
        .initializer = 0xC85CF4, .vtable = 0x43727F8,
        .calcView = {0x9B0BB4, 0xD10283FF}, .pathPack = {0x18AFE34, 0xD10243FF},
        .auxiliary = {0xC33688, 0xA9BA7BFD}, .mask = {0xC3257C, 0xA9BA7BFD},
        .composite = {0xC31AFC, 0xA9BA7BFD}, .destroy = {0x18AFBF0, 0xA9BC7BFD},
        .maskBody = 0,
        .layout = &kOlderLayout, .compositeNeedsDepth = false,
    },
    {
        .version = profiles::Version::V140,
        .allocatorSlot = 0x3959DD0, .textureFree = 0x1B4BE0,
        .nodeClear = 0x1D505B0, .nodeAdd = 0x1D53370, .nodeReverse = 0x1D50B1C,
        .selectedDraw = 0x1EF4300, .constructor = 0xB5C060,
        .initializer = 0xB5FC3C, .vtable = 0x36AAF40,
        .calcView = {0x4F9720, 0xD10283FF}, .pathPack = {0x1EF5880, 0x3941D448},
        .auxiliary = {0x1EF44D0, 0xA9BA7BFD}, .mask = {0x541958, 0xB9417808},
        .composite = {0x540D7C, 0xA9BA7BFD}, .destroy = {0x1EF4120, 0xA9BC7BFD},
        .maskBody = 0x541978,
        .layout = &kNewerLayout, .compositeNeedsDepth = false,
    },
    {
        .version = profiles::Version::V141,
        .allocatorSlot = 0x3954D90, .textureFree = 0x1936F0,
        .nodeClear = 0x1D42730, .nodeAdd = 0x1D454F0, .nodeReverse = 0x1D42C9C,
        .selectedDraw = 0x1EE47B0, .constructor = 0xB5747C,
        .initializer = 0xB5B058, .vtable = 0x36A5F40,
        .calcView = {0x5C5000, 0xD10283FF}, .pathPack = {0x1EE5D30, 0x3941D448},
        .auxiliary = {0x1EE4980, 0xA9BA7BFD}, .mask = {0x3A00F8, 0xB9417808},
        .composite = {0x39F51C, 0xA9BA7BFD}, .destroy = {0x1EE45D0, 0xA9BC7BFD},
        .maskBody = 0x3A0118,
        .layout = &kNewerLayout, .compositeNeedsDepth = false,
    },
    {
        .version = profiles::Version::V142,
        .allocatorSlot = 0x3956DD0, .textureFree = 0x110AA0,
        .nodeClear = 0x1D41690, .nodeAdd = 0x1D44450, .nodeReverse = 0x1D41BFC,
        .selectedDraw = 0x1EE3830, .constructor = 0xB40B30,
        .initializer = 0xB4494C, .vtable = 0x36A7F40,
        .calcView = {0x4A10C4, 0xD10283FF}, .pathPack = {0x1EE4DB0, 0x3941D448},
        .auxiliary = {0x1EE3A00, 0xA9BA7BFD}, .mask = {0x2DBBF4, 0xB9417808},
        .composite = {0x2DB018, 0xA9BA7BFD}, .destroy = {0x1EE3650, 0xA9BC7BFD},
        .maskBody = 0x2DBC14,
        .layout = &kNewerLayout, .compositeNeedsDepth = false,
    },
    {
        .version = profiles::Version::V143,
        .allocatorSlot = 0x3968E08, .textureFree = 0x1C1E40,
        .nodeClear = 0x1D44DC0, .nodeAdd = 0x1D47B80, .nodeReverse = 0x1D4532C,
        .selectedDraw = 0x1EEA1D8, .constructor = 0xB78C08,
        .initializer = 0xB7C8A8, .vtable = 0x36B9F40,
        .calcView = {0x1024CC, 0xD10283FF}, .pathPack = {0x1EEB758, 0x3941D448},
        .auxiliary = {0x1EEA3A8, 0xA9BA7BFD}, .mask = {0x7AAB44, 0xB9417808},
        .composite = {0x7A9F68, 0xA9BA7BFD}, .destroy = {0x1EE9FF8, 0xA9BC7BFD},
        .maskBody = 0x7AAB64,
        .layout = &kNewerLayout, .compositeNeedsDepth = false,
    },
}};
const NativePathProfile* g_profile = nullptr;

const NativePathProfile* findProfile() {
    const auto* active = profiles::active();
    if (!active) return nullptr;
    for (const auto& profile : kProfiles)
        if (profile.version == active->version) return &profile;
    return nullptr;
}

enum class Failure : unsigned { Allocation = 1, Initialization, Frame, Route,
    NodePool, Phase, Mask, PackedPath, Composite };
void fail(Failure reason, unsigned detail = 0) {
    std::uint64_t empty = 0;
    g_failure.compare_exchange_strong(empty, (std::uint64_t(reason) << 32) | detail,
                                      std::memory_order_release, std::memory_order_relaxed);
    g_preparedEpoch = 0;
}
template<class T> T read(const void* object, std::size_t offset) {
    if (!totk::engine::read_guard::admit(object, offset, sizeof(T))) return T{};
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template<class T> void write(void* object, std::size_t offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
template<class F> F native(std::uintptr_t offset) { return reinterpret_cast<F>(g_mainBase + offset); }
std::uintptr_t address(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
bool isOwner(const void* object) { return object && address(object) == g_nativeOwner; }

bool releaseTexture(std::size_t offset) {
    if (!g_renderer) return true;
    auto* texture = read<void*>(g_renderer, offset);
    if (!texture) return true;
    const auto slot = read<std::uintptr_t>(reinterpret_cast<void*>(g_mainBase), g_profile->allocatorSlot);
    auto* allocator = slot ? read<void*>(reinterpret_cast<void*>(slot), 0) : nullptr;
    if (!allocator) { fail(Failure::Composite, 1); return false; }
    native<void (*)(void*, void*)>(g_profile->textureFree)(allocator, texture);
    write<std::uintptr_t>(g_renderer, offset, 0);
    return true;
}

void releaseMasks() {
    if (!g_renderer) return;
    const auto& layout = *g_profile->layout;
    releaseTexture(layout.color0);
    releaseTexture(layout.color1);
    releaseTexture(layout.depth);
    g_maskedEpoch = 0;
}

bool fillNodes() {
    const auto& layout = *g_profile->layout;
    auto* list = g_renderer + layout.selected;
    native<void (*)(void*)>(g_profile->nodeClear)(list);
    if (read<unsigned>(list, 16) || read<unsigned>(list, 40) != 512 ||
        !read<std::uintptr_t>(list, 24) || !read<std::uintptr_t>(list, 32)) {
        fail(Failure::NodePool, 1); return false;
    }
    for (unsigned i = 0; i < g_path.count; ++i) {
        const auto& point = g_path.points[i];
        native<void (*)(void*, const void*, const float*)>(g_profile->nodeAdd)(list, &point.position, &point.phase);
        if (read<unsigned>(list, 16) != i + 1) { fail(Failure::NodePool, 2); return false; }
    }
    if (g_path.count >= 2) native<void (*)(void*)>(g_profile->nodeReverse)(list);
    write(list, 48, g_path.points[0].position);
    write(list, 96, std::uint32_t{UINT32_MAX});
    write(g_renderer, layout.endpoint, g_path.points[g_path.count - 1].position);
    return true;
}

HOOK_DEFINE_TRAMPOLINE(CalcViewHook) {
    static u64 Callback(void* renderer, unsigned view, void* parameters, void* scene, void* context) {
        const auto result = Orig(renderer, view, parameters, scene, context);
        if (!isOwner(renderer) || view != 0 || !g_renderer) return result;
        g_preparedEpoch = 0;
        if (!g_generation) return result;
        if (g_maskedEpoch || g_maskInProgress) { fail(Failure::Phase, 1); return result; }
        pure::PoseFrameHeader header;
        if (!pose_render::copyHeader(g_epoch, g_generation, header)) {
            fail(Failure::Frame); return result;
        }
        if (!g_publication.snapshot(g_route)) { fail(Failure::Route, 0x100); return result; }
        const auto built = pure::buildNativePathFrame(g_path, g_route, header, g_epoch);
        if (built != pure::NativePathStatus::Ready && built != pure::NativePathStatus::Empty) {
            fail(Failure::Route, static_cast<unsigned>(built)); return result;
        }
        if (built == pure::NativePathStatus::Empty) {
            g_preparedEpoch = g_epoch;
            return result;
        }
        if (!fillNodes()) return result;
        const auto& layout = *g_profile->layout;
        write(g_renderer, layout.mode, std::uint32_t{1});
        Orig(g_renderer, view, parameters, scene, context);
        write(g_renderer, layout.processed + 36, std::uint8_t{0});
        write(g_renderer, layout.processed + 76, std::uint8_t{0});
        write(g_renderer, layout.processed + 80, std::uint32_t{0});
        write(g_renderer, layout.processed + 84, std::uint32_t{g_path.count - 1u});
        if (read<unsigned>(g_renderer, 56) || read<std::uint8_t>(g_renderer, layout.secondaryCount) ||
            (read<std::uint8_t>(g_renderer, layout.debugFlags) & 8u)) {
            fail(Failure::Initialization, 2); return result;
        }
        g_preparedEpoch = g_epoch;
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(PathPackHook) {
    static u64 Callback(void* renderer, void* destination, void* list, void* processed) {
        const auto result = Orig(renderer, destination, list, processed);
        if (renderer == g_renderer && g_maskInProgress &&
            list == g_renderer + g_profile->layout->selected)
            g_packedCount = static_cast<std::uint32_t>(result);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(AuxiliaryPathsHook) {
    static u64 Callback(void* renderer, void* drawContext, void* sceneContext) {
        const auto result = Orig(renderer, drawContext, sceneContext);
        if (renderer != g_renderer || !g_maskInProgress || g_path.count < 2) return result;
        native<void (*)(void*, void*, void*)>(g_profile->selectedDraw)(g_renderer, drawContext, sceneContext);
        if (g_packedCount != g_path.count) fail(Failure::PackedPath, g_packedCount);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(MaskHook) {
    static u64 Callback(void* renderer, void* context, void* drawContext, unsigned view, void* buffers) {
        const auto result = Orig(renderer, context, drawContext, view, buffers);
        if (isOwner(renderer) && view == 0 && g_renderer && g_profile->compositeNeedsDepth &&
            g_maskedEpoch && g_maskedEpoch != g_epoch) releaseMasks();
        const auto& layout = *g_profile->layout;
        if (!isOwner(renderer) || view != 0 || !g_renderer || g_path.count < 2 ||
            !g_preparedEpoch || g_preparedEpoch != g_epoch) return result;
        if (g_maskedEpoch || read<std::uintptr_t>(g_renderer, layout.color0) ||
            read<std::uintptr_t>(g_renderer, layout.color1) ||
            read<std::uintptr_t>(g_renderer, layout.depth)) {
            fail(Failure::Phase, 2); return result;
        }
        g_maskInProgress = true;
        g_packedCount = 0;
        if (g_profile->maskBody)
            native<void (*)(void*, void*, void*, unsigned, void*)>(g_profile->maskBody)(
                g_renderer, context, drawContext, view, buffers);
        else Orig(g_renderer, context, drawContext, view, buffers);
        g_maskInProgress = false;
        g_maskedEpoch = g_epoch;
        if (!read<std::uintptr_t>(g_renderer, layout.color0) ||
            !read<std::uintptr_t>(g_renderer, layout.color1))
            fail(Failure::Mask);
        if (g_packedCount != g_path.count) fail(Failure::PackedPath, g_packedCount);
        const bool hasDepth = read<std::uintptr_t>(g_renderer, layout.depth) != 0;
        if (hasDepth != g_profile->compositeNeedsDepth) fail(Failure::Mask, hasDepth ? 2 : 3);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(CompositeHook) {
    static u64 Callback(void* renderer, void* drawContext, void* context, unsigned view, void* buffers) {
        const auto result = Orig(renderer, drawContext, context, view, buffers);
        if (!isOwner(renderer) || view != 0 || !g_renderer) return result;
        if (!g_maskedEpoch) {
            if (g_preparedEpoch == g_epoch && g_path.count >= 2) fail(Failure::Mask, 1);
            return result;
        }
        const bool draw = g_maskedEpoch == g_preparedEpoch && g_preparedEpoch == g_epoch;
        if (draw) {
            Orig(g_renderer, drawContext, context, view, buffers);
        } else fail(Failure::Phase, 3);
        if (draw && g_profile->compositeNeedsDepth) {
            releaseTexture(g_profile->layout->color0);
            releaseTexture(g_profile->layout->color1);
        } else releaseMasks();
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(RendererDestroyHook) {
    static u64 Callback(void* renderer) {
        if (isOwner(renderer) && g_renderer) {
            releaseMasks();
            auto* owned = g_renderer;
            auto* heap = g_heap;
            g_renderer = nullptr;
            g_heap = nullptr;
            g_nativeOwner = g_preparedEpoch = 0;
            Orig(owned);
            heap->free(owned);
        }
        return Orig(renderer);
    }
};
}

void initializeForScene(void* extension, void* scene, sead::Heap* heap) {
    if (!g_mainBase || !g_profile || !extension || !scene || !heap) return;
    if (g_ownerFault) { fail(Failure::Initialization, 1); return; }
    if (g_renderer) {
        g_ownerFault = true;
        g_renderer = nullptr;
        g_heap = nullptr;
        g_nativeOwner = g_maskedEpoch = 0;
        fail(Failure::Initialization, 1);
        return;
    }
    const auto owner = read<std::uintptr_t>(extension, 0xAC0);
    if (!owner) { fail(Failure::Initialization, 3); return; }
    const auto& layout = *g_profile->layout;
    auto* memory = static_cast<std::byte*>(heap->tryAlloc(layout.bytes, 8));
    if (!memory) { fail(Failure::Allocation); return; }
    native<void (*)(void*)>(g_profile->constructor)(memory);
    native<void (*)(void*, void*, void*, sead::Heap*)>(g_profile->initializer)(
        memory, scene, static_cast<std::byte*>(extension) + 0x740, heap);
    const bool valid = read<std::uintptr_t>(memory, 0) == g_mainBase + g_profile->vtable &&
        read<std::uintptr_t>(memory, 8) && read<std::uintptr_t>(memory, 104) && read<std::uintptr_t>(memory, 112) &&
        read<std::uintptr_t>(memory, 120) && read<std::uintptr_t>(memory, 128) &&
        read<std::uintptr_t>(memory, 136) && read<std::uintptr_t>(memory, layout.resource) &&
        read<unsigned>(memory, layout.selected + 40) == 512 &&
        read<std::uintptr_t>(memory, layout.selected + 32);
    if (!valid) {
        native<void (*)(void*)>(g_profile->destroy.offset)(memory);
        heap->free(memory);
        fail(Failure::Initialization, 4);
        return;
    }
    g_renderer = memory;
    g_heap = heap;
    g_nativeOwner = owner;
}

void beginFrame(std::uint64_t epoch, std::uint32_t generation) {
    g_epoch = epoch;
    g_generation = generation;
    g_preparedEpoch = 0;
    if (generation && !g_renderer) fail(Failure::Initialization, 5);
}
bool publish(const pure::NativePathRoute& route) {
    if (g_publication.publish(route)) {
        return true;
    }
    std::uint64_t empty = 0;
    g_failure.compare_exchange_strong(empty, (std::uint64_t(Failure::Route) << 32) | 0x101u,
                                      std::memory_order_release, std::memory_order_relaxed);
    return false;
}
void* maskForScreenFilter(std::uint64_t epoch, std::uint32_t generation) {
    if (!g_profile || !g_profile->compositeNeedsDepth || !g_renderer || !epoch ||
        epoch != g_epoch || generation != g_generation || g_maskedEpoch != epoch)
        return nullptr;
    auto* mask = read<void*>(g_renderer, g_profile->layout->depth);
    return mask;
}
void finishScreenFilter(void* mask) {
    if (mask && g_profile && g_renderer &&
        read<void*>(g_renderer, g_profile->layout->depth) == mask) releaseMasks();
}
std::uint64_t takeFailure() { return g_failure.exchange(0, std::memory_order_acq_rel); }


bool sitesValid(std::uintptr_t mainBase, std::size_t textSize) {
    const auto* profile = findProfile();
    if (!profile) {
        Logging.Log("[self-recall] NATIVE_PATH_SITE_REJECT reason=unknown-profile");
        return false;
    }
    const std::array sites{profile->calcView, profile->pathPack, profile->auxiliary,
                           profile->mask, profile->composite, profile->destroy};
    for (const auto& site : sites) {
        if (site.offset < 0 || static_cast<std::size_t>(site.offset) + sizeof(std::uint32_t) > textSize) {
            Logging.Log("[self-recall] NATIVE_PATH_SITE_REJECT site=0x%X reason=outside-text",
                        static_cast<unsigned>(site.offset));
            return false;
        }
        const auto found = read<std::uint32_t>(reinterpret_cast<void*>(mainBase), site.offset);
        if (found != site.word) {
            Logging.Log("[self-recall] NATIVE_PATH_SITE_REJECT site=0x%X expected=%08X found=%08X",
                        static_cast<unsigned>(site.offset), site.word, found);
            return false;
        }
    }
    return true;
}

void install(std::uintptr_t mainBase) {
    g_profile = findProfile();
    if (!g_profile) {
        Logging.Log("[self-recall] NATIVE_PATH_INSTALL_REJECT reason=unknown-profile");
        return;
    }
    g_mainBase = mainBase;
    CalcViewHook::InstallAtOffset(g_profile->calcView.offset);
    PathPackHook::InstallAtOffset(g_profile->pathPack.offset);
    AuxiliaryPathsHook::InstallAtOffset(g_profile->auxiliary.offset);
    MaskHook::InstallAtOffset(g_profile->mask.offset);
    CompositeHook::InstallAtOffset(g_profile->composite.offset);
    RendererDestroyHook::InstallAtOffset(g_profile->destroy.offset);
}
}
