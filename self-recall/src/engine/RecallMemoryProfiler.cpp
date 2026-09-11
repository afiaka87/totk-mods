#include "RecallMemoryProfiler.hpp"
#if SELF_RECALL_MEMORY_PROFILE
#include <lib.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include "RecallEquipmentAppearance.hpp"
#include "RecallArchiveHeap.hpp"
#include "RecallMemoryProfile.hpp"

namespace self_recall::memory_profile {
namespace {
std::uintptr_t g_main = 0;
std::atomic<void*> g_archive{nullptr};
std::atomic<bool> g_gameplay{false};
std::atomic<unsigned> g_largeLogs{0}, g_createLogs{0}, g_failures{0};
std::atomic<unsigned> g_createFailures{0}, g_bufferLogs{0};
std::atomic<std::uint64_t> g_archivePeak{0}, g_archiveCalls{0}, g_archiveFailures{0};
pure::HistoryMemoryUsage<pure::kHistoryCapacity> g_historyUsage;
std::uint64_t g_frames = 0, g_words = 0, g_constants = 0, g_compared = 0, g_same = 0;
std::uint64_t g_ticks = 0, g_maxTicks = 0, g_lastReport = 0;
unsigned g_peakBones = 0, g_peakModels = 0;
std::uint64_t g_captureCalls = 0, g_captureTicks = 0, g_captureMaxTicks = 0;

template<class T> T read(const void* pointer, unsigned offset) {
    T value; std::memcpy(&value, static_cast<const std::byte*>(pointer) + offset, sizeof(value));
    return value;
}
void maximum(std::atomic<std::uint64_t>& target, std::uint64_t value) {
    auto old = target.load(std::memory_order_relaxed);
    while (old < value && !target.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
}
struct HeapInfo { bool known = false; std::uint64_t size = 0, free = 0, largest = 0; };
HeapInfo inspect(void* heap, int alignment = 8) {
    if (!heap || read<std::uintptr_t>(heap, 0) != g_main + 0x045B7450) return {};
    if (!alignment || alignment == INT32_MIN) alignment = 8;
    const auto positive = static_cast<unsigned>(alignment < 0 ? -alignment : alignment);
    if (positive & (positive - 1)) alignment = 8;
    using Free = std::size_t (*)(void*);
    using Largest = std::size_t (*)(void*, int);
    return {true, read<std::uint64_t>(heap, 0x30),
        reinterpret_cast<Free>(g_main + 0x021C5A58)(heap),
        reinterpret_cast<Largest>(g_main + 0x00D8A12C)(heap, alignment)};
}
void physical(const char* phase) {
    u64 total = 0, used = 0;
    const auto totalRc = svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    const auto usedRc = svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    Logging.Log("[self-recall] MEM_PHYSICAL phase=%s total_rc=%u used_rc=%u total=%llu used=%llu free=%llu\n",
        phase, totalRc, usedRc, total, used, total >= used ? total - used : 0);
}
void archiveSample(void* heap) {
    const auto info = inspect(heap);
    if (info.known && info.size >= info.free) maximum(g_archivePeak, info.size - info.free);
}
HOOK_DEFINE_TRAMPOLINE(ProfileAllocHook) {
    static void* Callback(void* heap, std::size_t bytes, int alignment) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        auto* result = Orig(heap, bytes, alignment);
        if (heap == g_archive.load(std::memory_order_acquire)) {
            ++g_archiveCalls;
            if (!result) ++g_archiveFailures;
            archiveSample(heap);
        }
        const bool failed = !result;
        const bool logFailure = failed && g_failures.fetch_add(1) < 64;
        const bool logLarge = !failed && !g_gameplay.load(std::memory_order_relaxed) &&
                              bytes >= 1024 * 1024 && g_largeLogs.fetch_add(1) < 256;
        if (logFailure || logLarge) {
            const auto info = inspect(heap, alignment);
            Logging.Log("[self-recall] MEM_ALLOC heap=%p request=%llu align=%d result=%p caller=%llx known=%u size=%llu free_after=%llu largest_after=%llu\n",
                heap, static_cast<unsigned long long>(bytes), alignment, result,
                static_cast<unsigned long long>(caller - g_main), unsigned(info.known), info.size, info.free, info.largest);
            if (failed) physical("allocation_failure");
        }
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(ProfileGraphicsHeapHook) {
    static void* Callback(void* heap) {
        auto* infoSlot = read<void*>(reinterpret_cast<void*>(g_main), 0x0462DEB0);
        auto* manager = infoSlot ? read<void*>(infoSlot, 0) : nullptr;
        auto* defaultHeap = manager ? read<void*>(manager, 8) : nullptr;
        auto* threadSlot = read<void*>(reinterpret_cast<void*>(g_main), 0x0462DEE0);
        auto* threadManager = threadSlot ? read<void*>(threadSlot, 0) : nullptr;
        void* thread = nullptr;
        if (threadManager) {
            using GetTls = void* (*)(int);
            thread = reinterpret_cast<GetTls>(g_main + 0x02B17AE0)(read<int>(threadManager, 0x48));
        }
        auto* currentHeap = thread ? read<void*>(thread, 0x88) : defaultHeap;
        const auto info = inspect(heap ? heap : currentHeap);
        Logging.Log("[self-recall] MEM_GRAPHICS_ENTER heap=%p default=%p current=%p known=%u size=%llu free=%llu largest=%llu\n",
            heap, defaultHeap, currentHeap, unsigned(info.known), info.size, info.free, info.largest);
        physical("graphics_entry");
        return Orig(heap);
    }
};
HOOK_DEFINE_TRAMPOLINE(ProfileCreateHook) {
    static void* Callback(std::size_t bytes, const char* const* name, void* parent,
                          int alignment, int direction, bool locked) {
        const bool report = bytes >= 1024 * 1024 && g_createLogs.fetch_add(1) < 512;
        const auto before = report ? inspect(parent, alignment) : HeapInfo{};
        auto* result = Orig(bytes, name, parent, alignment, direction, locked);
        const bool failed = !result && g_createFailures.fetch_add(1) < 64;
        if (report || failed) {
            Logging.Log("[self-recall] MEM_CREATE name=%.48s parent=%p request=%llu align=%d direction=%d result=%p known=%u parent_size=%llu free_before=%llu largest_before=%llu\n",
                name && *name ? *name : "(null)", parent, static_cast<unsigned long long>(bytes),
                alignment, direction, result, unsigned(before.known), before.size, before.free, before.largest);
            if (!result) physical("create_failure");
        }
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(ProfileBufferHeapHook) {
    static void* Callback(void* buffer, std::size_t bytes, const char* const* name,
                          bool locked, void* extra) {
        auto* result = Orig(buffer, bytes, name, locked, extra);
        if (bytes >= 1024 * 1024 && g_bufferLogs.fetch_add(1) < 128) {
            Logging.Log("[self-recall] MEM_BUFFER_HEAP name=%.48s buffer=%p bytes=%llu result=%p\n",
                name && *name ? *name : "(null)", buffer, static_cast<unsigned long long>(bytes), result);
            physical("buffer_heap");
        }
        return result;
    }
};
}

void install(std::uintptr_t mainBase) {
    g_main = mainBase;
    ProfileAllocHook::InstallAtOffset(0x00B6E8E4);
    ProfileGraphicsHeapHook::InstallAtOffset(0x00EAE65C);
    ProfileCreateHook::InstallAtOffset(0x00880BD8);
    ProfileBufferHeapHook::InstallAtOffset(0x00E7CF70);
    Logging.Log("[self-recall] MEMORY_PROFILE_05 enabled history_frames=%u pose_reserved=%llu archive_reserved=%llu counters_reserved=%llu\n",
        unsigned(pure::kHistoryCapacity),
        static_cast<unsigned long long>(sizeof(pure::PoseHistorySlot) * pure::kHistoryCapacity +
                                       sizeof(pure::PoseHistory) + pure::kPosePayloadArenaBytes),
        static_cast<unsigned long long>(equipment::kArchiveHeapBytes),
        static_cast<unsigned long long>(sizeof(g_historyUsage)));
    physical("install");
}
void watchArchive(void* heap) {
    g_archive.store(heap, std::memory_order_release);
    if (heap) archiveSample(heap);
}
void recordPose(const pure::PoseHistory& history, pure::PoseFrameKey key) {
    const auto start = svcGetSystemTick();
    const auto current = history.acquire(key);
    if (!current) return;
    const auto& frame = *current.get();
    const auto& h = frame.header;
    g_gameplay.store(true, std::memory_order_relaxed);
    ++g_frames;
    const unsigned packed = sizeof(pure::PoseFrameHeader) + sizeof(pure::RecordedVisibility) +
        h.boneCount * sizeof(pure::RecordedBoneMatrix) + h.modelCount * sizeof(pure::RecordedModelPose);
    g_historyUsage.record(key.slot, packed, key.generation, history.count());
    g_peakBones = std::max(g_peakBones, unsigned(h.boneCount));
    g_peakModels = std::max(g_peakModels, unsigned(h.modelCount));
    const auto previous = history.before(key, 1);
    bool comparable = previous && previous.get()->header.modelCount == h.modelCount;
    if (comparable) for (unsigned i = 0; i < h.modelCount; ++i) {
        if (std::memcmp(&frame.models[i].identity, &previous.get()->models[i].identity,
                        sizeof(pure::RecordedModelIdentity))) { comparable = false; break; }
    }
    for (unsigned i = 0; i < h.boneCount; ++i) {
        for (auto word : frame.bones[i].words) {
            ++g_words;
            if (word == 0 || word == 0x3f800000) ++g_constants;
        }
        if (comparable) {
            ++g_compared;
            if (!std::memcmp(&frame.bones[i], &previous.get()->bones[i], sizeof(frame.bones[i]))) ++g_same;
        }
    }
    const auto elapsed = svcGetSystemTick() - start;
    g_ticks += elapsed; g_maxTicks = std::max(g_maxTicks, elapsed);
    if (g_frames != 1 && h.elapsedNanoseconds >= g_lastReport &&
        h.elapsedNanoseconds - g_lastReport < 10'000'000'000ULL) return;
    g_lastReport = h.elapsedNanoseconds;
    Logging.Log("[self-recall] MEM_POSE frames=%llu retained=%u packed=%llu peak_packed=%llu peak_frames=%u peak_bones=%u peak_models=%u words=%llu zero_or_one=%llu compared_bones=%llu identical_bones=%llu scan_ticks=%llu max_scan_ticks=%llu gaps=%u\n",
        g_frames, history.count(), g_historyUsage.bytes, g_historyUsage.peakBytes,
        g_historyUsage.peakFrames, g_peakBones, g_peakModels, g_words, g_constants, g_compared, g_same, g_ticks, g_maxTicks,
        g_historyUsage.gaps);
    const auto& payload = history.payloadUsage();
    Logging.Log("[self-recall] MEM_POSE_POOL live=%llu peak=%llu allocated=%llu peak_allocated=%llu capacity=%u storage_failures=%llu read_failures=%llu\n",
        payload.liveBytes, payload.peakBytes, payload.liveAllocated, payload.peakAllocated,
        pure::kPosePayloadArenaBytes, history.storageFailures(), history.readFailures());
    Logging.Log("[self-recall] MEM_CAPTURE calls=%llu ticks=%llu max_ticks=%llu\n",
        g_captureCalls, g_captureTicks, g_captureMaxTicks);
    equipment::logAppearanceMemory();
    const auto archive = inspect(g_archive.load(std::memory_order_acquire));
    Logging.Log("[self-recall] MEM_ARCHIVE known=%u size=%llu free=%llu largest=%llu peak_used=%llu allocs=%llu failures=%llu native_failures=%u large_logs=%u create_logs=%u\n",
        unsigned(archive.known), archive.size, archive.free, archive.largest,
        g_archivePeak.load(), g_archiveCalls.load(), g_archiveFailures.load(),
        g_failures.load(), g_largeLogs.load(), g_createLogs.load());
    physical("recording");
}
void recordCaptureTicks(std::uint64_t ticks) {
    ++g_captureCalls;
    g_captureTicks += ticks;
    g_captureMaxTicks = std::max(g_captureMaxTicks, ticks);
}
}
#endif
