#include "RecallArchiveHeap.hpp"
#include <atomic>
#include <lib.hpp>
#include "RecallMemoryProfiler.hpp"

namespace self_recall::equipment {
namespace {
alignas(4096) std::byte g_arena[kArchiveHeapBytes];
std::atomic<void*> g_heap{nullptr};
std::uintptr_t g_mainBase = 0;

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
    g_mainBase = mainBase;
    FindArchiveHeapHook::InstallAtOffset(0x007FE124);
}

void* archiveHeap() {
    auto* heap = g_heap.load(std::memory_order_acquire);
    if (heap || !g_mainBase) return heap;
    const char* name = "SelfRecallEquipment";
    using Create = void* (*)(void*, std::size_t, const char* const*, bool, void*);
    heap = reinterpret_cast<Create>(g_mainBase + 0x00E7CF70)(g_arena, sizeof(g_arena), &name, true, nullptr);
    g_heap.store(heap, std::memory_order_release);
    memory_profile::watchArchive(heap);
    Logging.Log("[self-recall] EQUIPMENT_HEAP ready=%u bytes=%llu", unsigned(heap != nullptr),
                static_cast<unsigned long long>(sizeof(g_arena)));
    return heap;
}

std::size_t contiguousArchiveBytes() {
    const auto* heap = g_heap.load(std::memory_order_acquire);
    if (!heap) return 0;
    using Size = std::size_t (*)(const void*, int);
    return reinterpret_cast<Size>(g_mainBase + 0x00D8A12C)(heap, 4096);
}

void* allocateArchive(std::size_t bytes) {
    auto* heap = archiveHeap();
    if (!heap || !bytes) return nullptr;
    using Allocate = void* (*)(void*, std::size_t, int);
    return reinterpret_cast<Allocate>(g_mainBase + 0x00B6E8E4)(heap, bytes, 8);
}

void freeArchive(void* address) {
    if (!address || !archiveOwns(address)) return;
    using Free = void (*)(void*, void*);
    reinterpret_cast<Free>(g_mainBase + 0x02A294DC)(g_heap.load(std::memory_order_acquire), address);
}
} // namespace self_recall::equipment
