// SPDX-License-Identifier: MIT
#include "SurveyStartupDiagnostic.hpp"

#if SURVEY_STARTUP_DIAGNOSTIC

#include <lib.hpp>
#include <heap/seadHeap.h>
#include <nn/fs.h>
#include <nn/util.h>
#include <nvn/nvn.h>

#include <atomic>
#include <cstddef>
#include <cstring>

#include "AtlasStartup.hpp"

namespace zonai_survey::engine::startup_diagnostic {
namespace {

constexpr std::uint64_t kCapacity = 393216;
nn::fs::FileHandle g_file{};
std::atomic<bool> g_ready{};
std::atomic_flag g_writer = ATOMIC_FLAG_INIT;
std::atomic<bool> g_argsReady{};
std::atomic<bool> g_atlasActivationAttempted{};
std::atomic<std::uint32_t> g_earlyState{};
std::atomic<std::uintptr_t> g_nativeArguments{};
std::uint64_t g_offset{};
std::uint64_t g_sequence{};
char g_path[128]{};
std::atomic_flag g_reportingFailure = ATOMIC_FLAG_INIT;
std::atomic<unsigned> g_newFailures{};
std::atomic<unsigned> g_allocFailures{};
std::atomic<unsigned> g_createFailures{};
std::atomic<unsigned> g_moduleVisits{};
std::atomic<unsigned> g_scalarFailures{}, g_driverFailures{}, g_reallocFailures{};
std::uintptr_t g_mainBase{};

// Entry signatures and first instructions verified in main121_named (1.2.1).
constexpr std::uintptr_t kAllocateWrapper = 0x00C12070;
constexpr std::uintptr_t kCreateHeap = 0x00880BD8;
constexpr std::uintptr_t kAllocateHeap = 0x00B6E8E4;
constexpr std::uintptr_t kPrepareModule = 0x007F67EC;

void processMemory(const char* stage) {
    u64 total{}, used{}, systemTotal{}, systemUsed{};
    const auto a = svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    const auto b = svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    const auto c = svcGetInfo(&systemTotal, InfoType_SystemResourceSizeTotal, CUR_PROCESS_HANDLE, 0);
    const auto d = svcGetInfo(&systemUsed, InfoType_SystemResourceSizeUsed, CUR_PROCESS_HANDLE, 0);
    mark(stage, total, used);
    mark("process-system-resource", systemTotal, systemUsed);
    mark("process-query-results", (std::uint64_t(a) << 32) | b,
         (std::uint64_t(c) << 32) | d);
}

void allocationFailure(const char* kind, std::atomic<unsigned>& count,
                       std::size_t bytes, sead::Heap* heap, std::int32_t alignment,
                       std::uintptr_t caller) {
    if (!g_ready.load(std::memory_order_acquire) ||
        g_reportingFailure.test_and_set(std::memory_order_acquire)) return;
    if (count.fetch_add(1, std::memory_order_relaxed) < 12) {
        mark(kind, bytes, static_cast<std::uint32_t>(alignment));
        mark("failure-caller-heap", caller, reinterpret_cast<std::uintptr_t>(heap));
        // Only inspect explicit heaps. A null default/current heap is itself evidence.
        if (heap) {
            mark("failure-heap-free", heap->getFreeSize(), heap->getMaxAllocatableSize(8));
        }
    }
    g_reportingFailure.clear(std::memory_order_release);
}

HOOK_DEFINE_TRAMPOLINE(AllocateWrapperHook) {
    static void* Callback(std::size_t bytes, sead::Heap* heap, std::int32_t alignment) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        auto* result = Orig(bytes, heap, alignment);
        if (!result) allocationFailure("new-null", g_newFailures, bytes, heap, alignment, caller);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(AllocateScalarHook) {
    static void* Callback(std::size_t bytes, sead::Heap* heap, std::int32_t alignment) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        auto* result = Orig(bytes, heap, alignment);
        if (!result) allocationFailure("scalar-new-null", g_scalarFailures, bytes, heap,
                                       alignment, caller);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(DriverAllocateHook) {
    static void* Callback(std::size_t bytes, std::size_t alignment, void* user) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        auto* result = Orig(bytes, alignment, user);
        if (!result) allocationFailure("driver-alloc-null", g_driverFailures, bytes, nullptr,
                                       static_cast<std::int32_t>(alignment), caller);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(DriverReallocateHook) {
    static void* Callback(void* previous, std::size_t bytes, void* user) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        auto* result = Orig(previous, bytes, user);
        if (!result && bytes) allocationFailure("driver-realloc-null", g_reallocFailures,
                                                bytes, nullptr, 0, caller);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(AbortImportHook) {
    static void Callback() {
        mark("native-abort", reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)), 0);
        processMemory("abort-process-memory");
        auto frame = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
        for (unsigned i = 0; i < 12 && frame && !(frame & 15); ++i) {
            MemoryInfo info{};
            u32 page{};
            if (svcQueryMemory(&info, &page, frame) || !(info.perm & 1) ||
                frame < info.addr || frame - info.addr > info.size ||
                info.size - (frame - info.addr) < 16) break;
            const auto* values = reinterpret_cast<const std::uintptr_t*>(frame);
            const auto next = values[0];
            mark("abort-frame", i, values[1]);
            if (next <= frame || next - frame > 0x100000) break;
            frame = next;
        }
        Orig();
    }
};

HOOK_DEFINE_TRAMPOLINE(AllocateHeapHook) {
    static void* Callback(sead::Heap* heap, std::size_t bytes, std::int32_t alignment) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        auto* result = Orig(heap, bytes, alignment);
        if (!result) allocationFailure("exp-alloc-null", g_allocFailures, bytes, heap, alignment, caller);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(CreateHeapHook) {
    static sead::Heap* Callback(std::size_t bytes, const void* name, sead::Heap* parent,
                               std::int32_t alignment, std::int32_t direction, bool lock) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        auto* result = Orig(bytes, name, parent, alignment, direction, lock);
        if (!result) allocationFailure("heap-create-null", g_createFailures, bytes,
                                       parent, alignment, caller);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(PrepareModuleHook) {
    static std::uintptr_t Callback(void* self, std::uint32_t module, std::uintptr_t modules,
        void* arguments, std::uintptr_t factory, std::uintptr_t extraArguments,
        std::uintptr_t budgets, std::uintptr_t parent, std::uintptr_t initArguments,
        std::uintptr_t phase, std::uintptr_t adjustment, std::uint8_t createExtra,
        std::uint8_t destroyExtra, std::uint8_t skipModule, std::int32_t mode) {
        const bool ready = g_ready.load(std::memory_order_acquire);
        const auto visit = ready ? g_moduleVisits.fetch_add(1, std::memory_order_relaxed) : 0;
        const bool trace = ready && visit < 512;
        if (ready && visit == 512) mark("module-trace-limit", module, phase);
        if (trace) mark("module-before", module, phase);
        const auto result = Orig(self, module, modules, arguments, factory, extraArguments,
            budgets, parent, initArguments, phase, adjustment, createExtra, destroyExtra,
            skipModule, mode);
        // Graphics preparation opens the file inside Orig, so retain its completion too.
        if (trace || (g_ready.load(std::memory_order_acquire) && g_moduleVisits.load() == 0)) {
            mark("module-after", module, phase);
        }
        return result;
    }
};

void gpuSummary();
HOOK_DEFINE_INLINE(ModulesCompleteHook) {
    static void Callback(exl::hook::InlineFloatCtx*) {
        mark("module-system-complete", g_moduleVisits.load(), 0);
        processMemory("modules-process-memory");
        gpuSummary();
    }
};

HOOK_DEFINE_TRAMPOLINE(ModulePrepareHook) {
    static std::uintptr_t Callback(void* system) {
        const auto result = Orig(system);
        mark("module-prepare-returned", reinterpret_cast<std::uintptr_t>(system), result);
        processMemory("prepare-return-memory");
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(DrawLayerHook) {
    static std::uintptr_t Callback(void* display, void* layer, unsigned index) {
        static std::atomic<unsigned> visits{};
        const auto visit = visits.fetch_add(1);
        if (visit < 24) mark("layer-before", visit, reinterpret_cast<std::uintptr_t>(layer));
        const auto result = Orig(display, layer, index);
        if (visit < 24) mark("layer-after", visit, reinterpret_cast<std::uintptr_t>(layer));
        if (visit == 24) mark("layer-trace-limit", visit, 0);
        return result;
    }
};

PFNNVNMEMORYPOOLINITIALIZEPROC g_poolInitialize{};
PFNNVNBUFFERINITIALIZEPROC g_bufferInitialize{};
PFNNVNBUFFERMAPPROC g_bufferMap{};
PFNNVNPROGRAMSETSHADERSPROC g_programShaders{};
PFNNVNTEXTUREINITIALIZEPROC g_textureInitialize{};
std::atomic<unsigned> g_gpuCalls{}, g_gpuFailures{};
std::atomic<bool> g_extraGraphics{};
PFNNVNQUEUESUBMITCOMMANDSPROC g_queueSubmit{};
PFNNVNQUEUEFLUSHPROC g_queueFlush{};
PFNNVNQUEUEFINISHPROC g_queueFinish{};
PFNNVNQUEUEPRESENTTEXTUREPROC g_queuePresent{};
PFNNVNQUEUEGETERRORPROC g_queueGetError{};
PFNNVNQUEUEFENCESYNCPROC g_queueFence{};
PFNNVNSYNCWAITPROC g_syncWait{};
std::atomic<unsigned> g_queueCalls{}, g_queueErrors{};

unsigned queueBefore(const char* stage, NVNqueue* queue) {
    const auto visit = g_queueCalls.fetch_add(1);
    if (visit < 128) mark(stage, visit, reinterpret_cast<std::uintptr_t>(queue));
    if (visit == 128) mark("queue-trace-limit", visit, 0);
    return visit;
}

void queueSubmit(NVNqueue* queue, int count, const NVNcommandHandle* commands) {
    const auto visit = queueBefore("queue-submit-before", queue);
    if (visit < 128) mark("queue-submit-count", visit, static_cast<unsigned>(count));
    if (visit < 128) mark("queue-submit-caller", visit,
        reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
    g_queueSubmit(queue, count, commands);
    if (visit < 128) mark("queue-submit-after", visit, reinterpret_cast<std::uintptr_t>(queue));
    if (visit == 0) processMemory("first-submit-return-memory");
}
void queueFlush(NVNqueue* queue) {
    const auto visit = queueBefore("queue-flush-before", queue);
    g_queueFlush(queue);
    if (visit < 128) mark("queue-flush-after", visit, reinterpret_cast<std::uintptr_t>(queue));
}
void queueFinish(NVNqueue* queue) {
    const auto visit = queueBefore("queue-finish-before", queue);
    g_queueFinish(queue);
    if (visit < 128) mark("queue-finish-after", visit, reinterpret_cast<std::uintptr_t>(queue));
}
void queuePresent(NVNqueue* queue, NVNwindow* window, int texture) {
    const auto visit = queueBefore("queue-present-before", queue);
    g_queuePresent(queue, window, texture);
    if (visit < 128) mark("queue-present-after", visit, reinterpret_cast<std::uintptr_t>(queue));
}
NVNqueueGetErrorResult queueGetError(NVNqueue* queue, NVNqueueErrorInfo* info) {
    // Forward the native caller's buffer; our header does not define its real size.
    const auto result = g_queueGetError(queue, info);
    if (result != NVN_QUEUE_GET_ERROR_RESULT_GPU_NO_ERROR && g_queueErrors.fetch_add(1) < 24) {
        mark("queue-native-error", static_cast<unsigned>(result),
             reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
        processMemory("queue-error-memory");
    }
    return result;
}

void queueFence(NVNqueue* queue, NVNsync* sync, NVNsyncCondition condition, int flags) {
    const auto visit = queueBefore("queue-fence-before", queue);
    if (visit < 128) mark("queue-fence-sync", reinterpret_cast<std::uintptr_t>(sync),
        (std::uint64_t(static_cast<unsigned>(condition)) << 32) | static_cast<unsigned>(flags));
    g_queueFence(queue, sync, condition, flags);
    if (visit < 128) mark("queue-fence-after", visit, reinterpret_cast<std::uintptr_t>(queue));
}

NVNsyncWaitResult syncWait(const NVNsync* sync, std::uint64_t timeout) {
    static std::atomic<unsigned> visits{};
    const auto visit = visits.fetch_add(1);
    if (visit < 64) mark("sync-wait-before", reinterpret_cast<std::uintptr_t>(sync), timeout);
    const auto result = g_syncWait(sync, timeout);
    if (visit < 64) mark("sync-wait-after", visit, static_cast<unsigned>(result));
    if (visit == 64) mark("sync-wait-trace-limit", visit, 0);
    return result;
}

void gpuResult(const char* name, bool success, std::uintptr_t caller) {
    const auto count = g_gpuCalls.fetch_add(1, std::memory_order_relaxed);
    const auto failure = !success ? g_gpuFailures.fetch_add(1, std::memory_order_relaxed) : 0;
    if ((g_extraGraphics.load() && count < 128) ||
        (!success && failure < 24)) {
        mark(name, success, caller);
        if (!success) processMemory("gpu-failure-memory");
    }
}

NVNboolean poolInitialize(NVNmemoryPool* pool, const NVNmemoryPoolBuilder* builder) {
    const auto result = g_poolInitialize(pool, builder);
    gpuResult("gpu-pool-result", result,
              reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
    return result;
}
NVNboolean bufferInitialize(NVNbuffer* buffer, const NVNbufferBuilder* builder) {
    const auto result = g_bufferInitialize(buffer, builder);
    gpuResult("gpu-buffer-result", result,
              reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
    return result;
}
void* bufferMap(const NVNbuffer* buffer) {
    auto* result = g_bufferMap(buffer);
    gpuResult("gpu-map-result", result != nullptr,
              reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
    return result;
}
NVNboolean programShaders(NVNprogram* program, int count, const NVNshaderData* stages) {
    const auto result = g_programShaders(program, count, stages);
    gpuResult("gpu-shaders-result", result,
              reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
    return result;
}
NVNboolean textureInitialize(NVNtexture* texture, const NVNtextureBuilder* builder) {
    const auto result = g_textureInitialize(texture, builder);
    gpuResult("gpu-texture-result", result,
              reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
    return result;
}

template<class F>
bool wrapGpu(F& original, F replacement, std::uintptr_t indirect, std::uintptr_t storage) {
    // Native loads the relocation cell, then the writable NVN dispatch pointer.
    const auto address = *reinterpret_cast<const std::uintptr_t*>(g_mainBase + indirect);
    if (address != g_mainBase + storage) {
        mark("gpu-slot-refused", indirect, address);
        return false;
    }
    auto* slot = reinterpret_cast<F*>(address);
    original = *slot;
    if (!original || original == replacement) {
        mark("gpu-target-refused", indirect, reinterpret_cast<std::uintptr_t>(original));
        return false;
    }
    std::atomic_ref<F>(*slot).store(replacement, std::memory_order_release);
    return true;
}

bool installGpuObservers() {
    return wrapGpu(g_poolInitialize, &poolInitialize, 0x046170A0, 0x04670AD0) &&
        wrapGpu(g_bufferInitialize, &bufferInitialize, 0x04616EE0, 0x04670BD8) &&
        wrapGpu(g_bufferMap, &bufferMap, 0x04616EF8, 0x04670BF0) &&
        wrapGpu(g_programShaders, &programShaders, 0x04617368, 0x04670A70) &&
        wrapGpu(g_textureInitialize, &textureInitialize, 0x04617650, 0x04670E00) &&
        wrapGpu(g_queueSubmit, &queueSubmit, 0x04617270, 0x04670978) &&
        wrapGpu(g_queueFlush, &queueFlush, 0x04617278, 0x04670980) &&
        wrapGpu(g_queueFinish, &queueFinish, 0x04617280, 0x04670988) &&
        wrapGpu(g_queuePresent, &queuePresent, 0x04617288, 0x04670990) &&
        wrapGpu(g_queueGetError, &queueGetError, 0x04617198, 0x046708A0) &&
        wrapGpu(g_queueFence, &queueFence, 0x04617E30, 0x04671778) &&
        wrapGpu(g_syncWait, &syncWait, 0x04617E38, 0x04671780);
}

void gpuSummary() {
    mark("gpu-calls-failures", g_gpuCalls.load(), g_gpuFailures.load());
    mark("gpu-observers-retained",
         *reinterpret_cast<PFNNVNMEMORYPOOLINITIALIZEPROC*>(g_mainBase + 0x04670AD0) == &poolInitialize,
         *reinterpret_cast<PFNNVNBUFFERMAPPROC*>(g_mainBase + 0x04670BF0) == &bufferMap);
}

bool activateAtlasInput(sead::Heap* heap) {
    if (g_atlasActivationAttempted.exchange(true, std::memory_order_acq_rel)) {
        mark("atlas-activation-repeat-refused");
        return false;
    }
    mark("atlas-native-font-bypassed", heap ? heap->getFreeSize() : 0, 0);
    g_atlasInputReady.store(true, std::memory_order_release);
    return true;
}

void stop(const char* operation, unsigned result) {
    if (g_ready.exchange(false, std::memory_order_acq_rel)) nn::fs::CloseFile(g_file);
    Logging.Log("[survey-diag] status stopped operation=%s result=%08x", operation, result);
}

}

void mark(const char* stage, std::uint64_t value0, std::uint64_t value1) {
    if (!g_ready.load(std::memory_order_acquire) ||
        g_writer.test_and_set(std::memory_order_acquire)) return;

    char line[192]{};
    const int written = nn::util::SNPrintf(
        line, sizeof(line), "%04llu tick=%016llx %s value0=%016llx value1=%016llx\n",
        static_cast<unsigned long long>(g_sequence++),
        static_cast<unsigned long long>(svcGetSystemTick()),
        stage ? stage : "null-stage",
        static_cast<unsigned long long>(value0),
        static_cast<unsigned long long>(value1));
    const auto bytes = written > 0 ? static_cast<std::size_t>(written) : 0;
    if (!bytes || bytes >= sizeof(line) || g_offset + bytes > kCapacity) {
        stop("capacity", 0);
        g_writer.clear(std::memory_order_release);
        return;
    }
    auto result = nn::fs::WriteFile(g_file, static_cast<s64>(g_offset), line, bytes,
                                    nn::fs::WriteOption{0});
    if (result.IsFailure()) {
        stop("write", result.GetInnerValueForDebug());
        g_writer.clear(std::memory_order_release);
        return;
    }
    result = nn::fs::FlushFile(g_file);
    if (result.IsFailure()) {
        stop("flush", result.GetInnerValueForDebug());
        g_writer.clear(std::memory_order_release);
        return;
    }
    g_offset += bytes;
    g_writer.clear(std::memory_order_release);
}

void begin() {
    if (g_ready.load(std::memory_order_acquire)) return;
    auto result = nn::fs::MountSdCard("surveydiag");
    if (result.IsFailure()) {
        Logging.Log("[survey-diag] SD mount failed result=%08x", result.GetInnerValueForDebug());
        return;
    }
    const auto nonce = static_cast<unsigned long long>(svcGetSystemTick());
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        nn::util::SNPrintf(g_path, sizeof(g_path),
            "surveydiag:/survey-startup-diag-%016llx-%02u.log", nonce, attempt);
        result = nn::fs::CreateFile(g_path, static_cast<s64>(kCapacity));
        if (result.IsSuccess()) break;
    }
    if (result.IsFailure()) {
        Logging.Log("[survey-diag] status create failed result=%08x", result.GetInnerValueForDebug());
        return;
    }
    result = nn::fs::OpenFile(&g_file, g_path, nn::fs::OpenMode_Write);
    if (result.IsFailure()) {
        Logging.Log("[survey-diag] status open failed result=%08x", result.GetInnerValueForDebug());
        return;
    }
    g_ready.store(true, std::memory_order_release);
    mark("trace-open", kCapacity, 0);
    mark("failure-hooks-main", g_mainBase, SURVEY_ATLAS_RENDERER ? 21 : 15);
}

void installFailureHooks(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    AllocateWrapperHook::InstallAtOffset(kAllocateWrapper);
    AllocateScalarHook::InstallAtOffset(0x007FA8F0);
    DriverAllocateHook::InstallAtOffset(0x0074C140);
    DriverReallocateHook::InstallAtOffset(0x00754E30);
    AbortImportHook::InstallAtOffset(0x02B18480);
    AllocateHeapHook::InstallAtOffset(kAllocateHeap);
    CreateHeapHook::InstallAtOffset(kCreateHeap);
    PrepareModuleHook::InstallAtOffset(kPrepareModule);
    ModulesCompleteHook::InstallAtOffset(0x007F6614);
    ModulePrepareHook::InstallAtOffset(0x007F4CC8);
    DrawLayerHook::InstallAtOffset(0x00817F90);
}

void captureCreateArgs(std::uintptr_t nativeArguments, std::uintptr_t debugArguments) {
    g_earlyState.store(1u | (nativeArguments ? 2u : 0u) |
                           (debugArguments ? 4u : 0u), std::memory_order_release);
    if (!nativeArguments || !debugArguments) return;
    g_nativeArguments.store(nativeArguments, std::memory_order_release);
    g_argsReady.store(true, std::memory_order_release);
}

void lateGraphicsInit() {
    if (!g_ready.load(std::memory_order_acquire)) begin();
    if (!g_ready.load(std::memory_order_acquire)) return;
    const bool argsReady = g_argsReady.load(std::memory_order_acquire);
    mark("late-hook", g_earlyState.load(std::memory_order_acquire), argsReady ? 1 : 0);
    if (!argsReady) {
        mark("late-refused-precondition");
        return;
    }
    mark("atlas-zero-startup-gpu-alloc", 1, 0);
    mark("atlas-no-font-no-textwriter", 1, 0);
    const auto nativeArguments = g_nativeArguments.load(std::memory_order_acquire);
    auto* graphicsHeap = nativeArguments
        ? *reinterpret_cast<sead::Heap* const*>(nativeArguments) : nullptr;
    auto* nativeDebugHeap = nativeArguments
        ? reinterpret_cast<sead::Heap* const*>(nativeArguments)[2] : nullptr;
    auto* publishedHeap = g_atlasObservedHeap;
    mark("heap-selection", graphicsHeap == publishedHeap ? 1 : 0,
         nativeDebugHeap == publishedHeap ? 1 : 0);
    if (!graphicsHeap || !publishedHeap) {
        mark("late-refused-precondition", graphicsHeap ? 1 : 0,
             publishedHeap ? 1 : 0);
        return;
    }
    mark("graphics-heap-size", graphicsHeap->getSize(), 0);
    mark("native-graphics-headroom", graphicsHeap->getFreeSize(),
         graphicsHeap->getMaxAllocatableSize(8));
    mark("published-heap-size", publishedHeap->getSize(),
         nativeDebugHeap ? 1 : 0);
    const auto freeBytes = publishedHeap->getFreeSize();
    const auto maxBytes = publishedHeap->getMaxAllocatableSize(8);
    mark("published-headroom", freeBytes, maxBytes);
    mark("before-extra-graphics", freeBytes, maxBytes);
    processMemory("before-process-memory");
    if (!installGpuObservers()) {
        mark("gpu-observers-refused");
        return;
    }
    g_extraGraphics.store(true);
    activateAtlasInput(publishedHeap);
    g_extraGraphics.store(false);
    mark("after-extra-graphics", g_atlasInputReady.load() ? 1 : 0, 0);
    mark("published-headroom-after", publishedHeap->getFreeSize(),
         publishedHeap->getMaxAllocatableSize(8));
    mark("native-graphics-after", graphicsHeap->getFreeSize(),
         graphicsHeap->getMaxAllocatableSize(8));
    processMemory("after-process-memory");
}

} // namespace zonai_survey::engine::startup_diagnostic

#endif
