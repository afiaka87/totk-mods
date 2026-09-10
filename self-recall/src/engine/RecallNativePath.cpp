#include "RecallNativePath.hpp"

#include <atomic>
#include <cstring>
#include <heap/seadHeap.h>
#include <lib.hpp>

#include "RecallFrameTicket.hpp"
#include "RecallPoseRender.hpp"

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
std::uint64_t g_calculations = 0, g_masks = 0, g_ribbons = 0, g_composites = 0;
std::uint32_t g_packedCount = 0;
bool g_maskInProgress = false;
bool g_ownerFault = false;
constexpr std::size_t kRendererBytes = 11216; // Native allocation C78698.
constexpr std::size_t kSelected = 10848;
constexpr std::size_t kProcessed = 10968;

enum class Failure : unsigned { Allocation = 1, Initialization, Frame, Route,
    NodePool, Phase, Mask, PackedPath, Composite };
void fail(Failure reason, unsigned detail = 0) {
    std::uint64_t empty = 0;
    g_failure.compare_exchange_strong(empty, (std::uint64_t(reason) << 32) | detail,
                                      std::memory_order_release, std::memory_order_relaxed);
    g_preparedEpoch = 0;
}
template<class T> T read(const void* object, std::size_t offset) {
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

void releaseMasks() {
    if (!g_renderer) return;
    const auto slot = read<std::uintptr_t>(reinterpret_cast<void*>(g_mainBase), 0x04638208);
    auto* allocator = slot ? read<void*>(reinterpret_cast<void*>(slot), 0) : nullptr;
    for (const auto offset : {0x158u, 0x160u, 0x168u}) {
        auto* texture = read<void*>(g_renderer, offset);
        if (!texture) continue;
        if (!allocator) { fail(Failure::Composite, 1); continue; }
        native<void (*)(void*, void*)>(0x007EF80C)(allocator, texture);
        write<std::uintptr_t>(g_renderer, offset, 0);
    }
    g_maskedEpoch = 0;
}

bool fillNodes() {
    auto* list = g_renderer + kSelected;
    native<void (*)(void*)>(0x01757E08)(list);
    if (read<unsigned>(list, 16) || read<unsigned>(list, 40) != 512 ||
        !read<std::uintptr_t>(list, 24) || !read<std::uintptr_t>(list, 32)) {
        fail(Failure::NodePool, 1); return false;
    }
    for (unsigned i = 0; i < g_path.count; ++i) {
        const auto& point = g_path.points[i];
        native<void (*)(void*, const void*, const float*)>(0x0175A038)(list, &point.position, &point.phase);
        if (read<unsigned>(list, 16) != i + 1) { fail(Failure::NodePool, 2); return false; }
    }
    if (g_path.count >= 2) native<void (*)(void*)>(0x017583E4)(list);
    write(list, 48, g_path.points[0].position);
    write(list, 96, std::uint32_t{UINT32_MAX}); // No native object-head interpolation.
    write(g_renderer, 11116, g_path.points[g_path.count - 1].position);
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
            ++g_calculations;
            return result;
        }
        if (!fillNodes()) return result;
        write(g_renderer, 372, std::uint32_t{1});
        Orig(g_renderer, view, parameters, scene, context);
        write(g_renderer, kProcessed + 36, std::uint8_t{0});
        write(g_renderer, kProcessed + 76, std::uint8_t{0});
        write(g_renderer, kProcessed + 80, std::uint32_t{0});
        write(g_renderer, kProcessed + 84, std::uint32_t{g_path.count - 1u});
        if (read<unsigned>(g_renderer, 56) || read<std::uint8_t>(g_renderer, 11136) ||
            (read<std::uint8_t>(g_renderer, 11202) & 8u)) {
            fail(Failure::Initialization, 2); return result;
        }
        g_preparedEpoch = g_epoch;
        ++g_calculations;
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(PathPackHook) {
    static u64 Callback(void* renderer, void* destination, void* list, void* processed) {
        const auto result = Orig(renderer, destination, list, processed);
        if (renderer == g_renderer && g_maskInProgress && list == g_renderer + kSelected)
            g_packedCount = static_cast<std::uint32_t>(result);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(AuxiliaryPathsHook) {
    static u64 Callback(void* renderer, void* drawContext, void* sceneContext) {
        const auto result = Orig(renderer, drawContext, sceneContext);
        if (renderer != g_renderer || !g_maskInProgress || g_path.count < 2) return result;
        native<void (*)(void*, void*, void*)>(0x00C334E4)(g_renderer, drawContext, sceneContext);
        if (g_packedCount != g_path.count) fail(Failure::PackedPath, g_packedCount);
        else ++g_ribbons;
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(MaskHook) {
    static u64 Callback(void* renderer, void* context, void* drawContext, unsigned view, void* buffers) {
        const auto result = Orig(renderer, context, drawContext, view, buffers);
        if (!isOwner(renderer) || view != 0 || !g_renderer || g_path.count < 2 ||
            !g_preparedEpoch || g_preparedEpoch != g_epoch) return result;
        if (g_maskedEpoch || read<std::uintptr_t>(g_renderer, 0x158) ||
            read<std::uintptr_t>(g_renderer, 0x160) || read<std::uintptr_t>(g_renderer, 0x168)) {
            fail(Failure::Phase, 2); return result;
        }
        g_maskInProgress = true;
        g_packedCount = 0;
        Orig(g_renderer, context, drawContext, view, buffers);
        g_maskInProgress = false;
        g_maskedEpoch = g_epoch;
        if (!read<std::uintptr_t>(g_renderer, 0x158) || !read<std::uintptr_t>(g_renderer, 0x160))
            fail(Failure::Mask);
        if (g_packedCount != g_path.count) fail(Failure::PackedPath, g_packedCount);
        if (read<std::uintptr_t>(g_renderer, 0x168)) fail(Failure::Mask, 2);
        ++g_masks;
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
        if (g_maskedEpoch == g_preparedEpoch && g_preparedEpoch == g_epoch) {
            Orig(g_renderer, drawContext, context, view, buffers);
            ++g_composites;
        } else fail(Failure::Phase, 3);
        releaseMasks();
        if (g_composites == 1 || (g_composites && g_composites % 1800 == 0))
            Logging.Log("[self-recall] NATIVE_PATH epoch=%llu frame=%llu points=%u packed=%u masks=%llu ribbons=%llu composites=%llu",
                static_cast<unsigned long long>(g_epoch), static_cast<unsigned long long>(g_path.key.serial),
                g_path.count, g_packedCount, static_cast<unsigned long long>(g_masks),
                static_cast<unsigned long long>(g_ribbons), static_cast<unsigned long long>(g_composites));
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
} // namespace

void initializeForScene(void* extension, void* scene, sead::Heap* heap) {
    if (!g_mainBase || !extension || !scene || !heap) return;
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
    auto* memory = static_cast<std::byte*>(heap->tryAlloc(kRendererBytes, 8));
    if (!memory) { fail(Failure::Allocation); return; }
    native<void (*)(void*)>(0x00C96E04)(memory);
    native<void (*)(void*, void*, void*, sead::Heap*)>(0x00C85CF4)(
        memory, scene, static_cast<std::byte*>(extension) + 0x740, heap);
    const bool valid = read<std::uintptr_t>(memory, 0) == g_mainBase + 0x043727F8 &&
        read<std::uintptr_t>(memory, 8) && read<std::uintptr_t>(memory, 104) && read<std::uintptr_t>(memory, 112) &&
        read<std::uintptr_t>(memory, 120) && read<std::uintptr_t>(memory, 128) &&
        read<std::uintptr_t>(memory, 136) && read<std::uintptr_t>(memory, 336) &&
        read<unsigned>(memory, kSelected + 40) == 512 && read<std::uintptr_t>(memory, kSelected + 32);
    if (!valid) {
        native<void (*)(void*)>(0x018AFBF0)(memory);
        heap->free(memory);
        fail(Failure::Initialization, 4);
        return;
    }
    g_renderer = memory;
    g_heap = heap;
    g_nativeOwner = owner;
    Logging.Log("[self-recall] NATIVE_PATH_STORAGE owner_bytes=%u selected_capacity=512",
                static_cast<unsigned>(kRendererBytes));
}

void beginFrame(std::uint64_t epoch, std::uint32_t generation) {
    if (g_generation && !generation)
        Logging.Log("[self-recall] NATIVE_PATH_SUMMARY calc=%llu masks=%llu ribbons=%llu composites=%llu",
            static_cast<unsigned long long>(g_calculations), static_cast<unsigned long long>(g_masks),
            static_cast<unsigned long long>(g_ribbons), static_cast<unsigned long long>(g_composites));
    g_epoch = epoch;
    g_generation = generation;
    g_preparedEpoch = 0;
    if (generation && !g_renderer) fail(Failure::Initialization, 5);
}
bool publish(const pure::NativePathRoute& route) {
    if (g_publication.publish(route)) return true;
    std::uint64_t empty = 0;
    g_failure.compare_exchange_strong(empty, (std::uint64_t(Failure::Route) << 32) | 0x101u,
                                      std::memory_order_release, std::memory_order_relaxed);
    return false;
}
std::uint64_t takeFailure() { return g_failure.exchange(0, std::memory_order_acq_rel); }
void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    CalcViewHook::InstallAtOffset(0x009B0BB4);
    PathPackHook::InstallAtOffset(0x018AFE34);
    AuxiliaryPathsHook::InstallAtOffset(0x00C33688);
    MaskHook::InstallAtOffset(0x00C3257C);
    CompositeHook::InstallAtOffset(0x00C31AFC);
    RendererDestroyHook::InstallAtOffset(0x018AFBF0);
}
} // namespace self_recall::native_path
