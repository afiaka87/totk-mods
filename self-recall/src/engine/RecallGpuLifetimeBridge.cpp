#include "RecallGpuLifetimeBridge.hpp"

#include <atomic>
#include <cstring>
#include <lib.hpp>

namespace self_recall::gpu_lifetime {
namespace {
std::uintptr_t g_mainBase = 0;
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
pure::RecallGpuLifetime g_lifetime;
std::atomic<bool> g_requested{false}, g_observing{false}, g_bindingsAllowed{false};
std::uintptr_t g_framework = 0, g_queue = 0, g_sync = 0;
unsigned g_failureOperation = 0; // Protected by Access, like the ledger.
std::uintptr_t g_rootCommandBuffer = 0, g_lastListCaller = 0;
std::uint64_t g_listBeginsSeen = 0, g_listBeginsTracked = 0, g_bindRejections = 0;
std::uint64_t g_layerBegins = 0, g_modelCopies = 0, g_aliasCopies = 0, g_displayCopies = 0;
std::uint64_t g_directSubmits = 0, g_fences = 0, g_completedWaits = 0;
struct TransferFailure {
    std::uintptr_t commandBuffer = 0, object = 0;
    unsigned sourceMask = 0, recorder = 0, rootRecording = 0;
} g_transferFailure;
bool g_failureLogged = false;

void observeTransfer(std::uintptr_t cb, std::uintptr_t object, Operation operation) {
    Access access;
    const auto mask = g_lifetime.listMask(object);
    const auto recorder = unsigned(g_lifetime.hasRecorder(cb));
    const auto rootRecording = unsigned(g_lifetime.hasRecordingFrame(cb));
    if (!g_lifetime.callList(cb, object) && !g_failureOperation)
        g_transferFailure = {cb, object, mask, recorder, rootRecording};
    access.noteFailure(operation);
}

template<class T> T read(const void* object, std::size_t offset) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof(result));
    return result;
}
std::uintptr_t address(const void* value) { return reinterpret_cast<std::uintptr_t>(value); }

bool matchesFramework(const void* framework) {
    return framework && address(framework) == g_framework &&
        read<std::uintptr_t>(framework, 0x1B0) == g_queue &&
        read<std::uintptr_t>(framework, 0x1D8) == g_sync;
}

HOOK_DEFINE_TRAMPOLINE(FrameworkDrawHook) {
    static u64 Callback(void* framework) {
        if (g_requested.load(std::memory_order_acquire) && framework) {
            const auto flags = read<std::uint8_t>(framework, 0x222);
            if (!(flags & 1u) || (flags & 2u)) {
                Access access;
                if (g_failureOperation && !g_failureLogged) {
                    g_failureLogged = true;
                    Logging.Log("[self-recall] GPU_LIFETIME_FAILED operation=%u cb=%p object=%p source_mask=%u recorder=%u root_recording=%u root=%p model_lists=%llu layer_lists=%llu model_copies=%llu aliases=%llu display_copies=%llu direct=%llu fences=%llu waits=%llu",
                        g_failureOperation, reinterpret_cast<void*>(g_transferFailure.commandBuffer),
                        reinterpret_cast<void*>(g_transferFailure.object), g_transferFailure.sourceMask,
                        g_transferFailure.recorder, g_transferFailure.rootRecording,
                        reinterpret_cast<void*>(g_rootCommandBuffer),
                        static_cast<unsigned long long>(g_listBeginsTracked - g_layerBegins),
                        static_cast<unsigned long long>(g_layerBegins), static_cast<unsigned long long>(g_modelCopies),
                        static_cast<unsigned long long>(g_aliasCopies), static_cast<unsigned long long>(g_displayCopies),
                        static_cast<unsigned long long>(g_directSubmits), static_cast<unsigned long long>(g_fences),
                        static_cast<unsigned long long>(g_completedWaits));
                }
                const auto queue = read<std::uintptr_t>(framework, 0x1B0);
                const auto sync = read<std::uintptr_t>(framework, 0x1D8);
                const auto cb = read<std::uintptr_t>(framework, 0x170);
                if (!g_framework && queue && sync && cb) {
                    g_framework = address(framework);
                    g_queue = queue;
                    g_sync = sync;
                }
                if (!matchesFramework(framework)) g_lifetime.freeze();
                else {
                    g_rootCommandBuffer = cb;
                    g_lifetime.beginFrame(cb);
                }
                access.noteFailure(Operation::FrameBegin);
                g_observing.store(true, std::memory_order_release);
            }
        }
        return Orig(framework);
    }
};

HOOK_DEFINE_INLINE(FrameSealHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_observing.load(std::memory_order_acquire)) return;
        const auto* framework = reinterpret_cast<const void*>(ctx->X[19]);
        Access access;
        if (!matchesFramework(framework)) g_lifetime.freeze();
        else g_lifetime.sealFrame(read<std::uintptr_t>(framework, 0x170), ctx->X[0]);
        access.noteFailure(Operation::FrameSeal);
    }
};

HOOK_DEFINE_TRAMPOLINE(FrameworkPresentHook) {
    static u64 Callback(void* framework) {
        if (g_observing.load(std::memory_order_acquire) && framework &&
            read<std::uint8_t>(framework, 0x223)) {
            Access access;
            if (!matchesFramework(framework)) g_lifetime.freeze();
            else g_lifetime.beginSubmission(read<std::uint64_t>(framework, 0x1E8));
            access.noteFailure(Operation::Submit);
        }
        return Orig(framework);
    }
};

using NativeFence = void (*)(void*, void*, unsigned, unsigned);
void fenceWithReceipt(void* queue, void* sync, unsigned condition, unsigned flags,
                      void* framework, NativeFence original) {
    std::uint64_t ticket;
    {
        Access access;
        if (!matchesFramework(framework) || address(queue) != g_queue || address(sync) != g_sync)
            g_lifetime.freeze();
        ticket = g_lifetime.captureDirectFence();
        access.noteFailure(Operation::FencePrepare);
    }
    original(queue, sync, condition, flags);
    {
        Access access;
        if (matchesFramework(framework))
            g_lifetime.submittedAndFenced(read<std::uint64_t>(framework, 0x1E8), g_queue, g_sync, ticket);
        else g_lifetime.freeze();
        ++g_fences;
        access.noteFailure(Operation::Fence);
    }
}
HOOK_DEFINE_INLINE(FenceCallHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_observing.load(std::memory_order_acquire)) return;
        ctx->X[4] = ctx->X[19];
        ctx->X[5] = ctx->X[8];
        ctx->X[8] = reinterpret_cast<std::uintptr_t>(&fenceWithReceipt);
    }
};

using NativeWait = std::uint32_t (*)(void*, std::uint64_t);
std::uint32_t waitWithReceipt(void* sync, std::uint64_t timeout, NativeWait original) {
    std::uint64_t ticket;
    {
        Access access;
        ticket = g_lifetime.captureWait(g_queue, address(sync));
    }
    const auto result = original(sync, timeout);
    {
        Access access;
        if (g_lifetime.completeWait(ticket, result)) ++g_completedWaits;
    }
    return result;
}

HOOK_DEFINE_INLINE(WaitCallHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_observing.load(std::memory_order_acquire)) return;
        ctx->X[2] = ctx->X[8];
        ctx->X[8] = reinterpret_cast<std::uintptr_t>(&waitWithReceipt);
    }
};

HOOK_DEFINE_TRAMPOLINE(ListBeginHook) {
    static u64 Callback(void* pool, void* drawContext, void* a3, void* a4,
                        std::uint32_t a5, std::uint64_t a6, int a7) {
        const auto caller = address(__builtin_return_address(0));
        const auto result = Orig(pool, drawContext, a3, a4, a5, a6, a7);
        if (g_bindingsAllowed.load(std::memory_order_acquire) && pool && drawContext) {
            Access access;
            ++g_listBeginsSeen;
            g_lastListCaller = caller >= g_mainBase ? caller - g_mainBase : caller;
            if (!site::isRenderListBegin(g_mainBase, caller)) return result;
            const auto objects = read<std::uintptr_t>(pool, 0x120);
            const auto count = read<std::uint32_t>(pool, 0x118);
            const auto cb = read<std::uintptr_t>(drawContext, 0xB8);
            if (g_lifetime.configurePool(address(pool), objects, count, 120)) {
                if (g_lifetime.beginList(cb, address(pool))) {
                    if (!site::isModelListBegin(g_mainBase, caller)) ++g_layerBegins;
                    if (++g_listBeginsTracked == 1)
                        Logging.Log("[self-recall] GPU_LIST_TRACKING caller=%llx cb=%p pool=%p capacity=%u stride=120",
                            static_cast<unsigned long long>(g_lastListCaller),
                            reinterpret_cast<void*>(cb), pool, count);
                }
                access.noteFailure(Operation::ListBegin);
            } else access.noteFailure(Operation::PoolConfigure);
        }
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ListEndHook) {
    static u64 Callback(void* pool, std::uint32_t index) {
        std::uintptr_t cb = 0;
        if (g_bindingsAllowed.load(std::memory_order_acquire) && pool) {
            const auto count = read<std::uint32_t>(pool, 0x118);
            const auto* objects = read<const std::byte*>(pool, 0x120);
            if (objects && count && index < count) {
                const auto* context = read<const void*>(objects + std::size_t{index} * 120, 0x30);
                if (context) cb = read<std::uintptr_t>(context, 0xB8);
            }
        }
        const auto result = Orig(pool, index);
        if (cb) {
            Access access;
            if (g_lifetime.hasRecorder(cb)) g_lifetime.endList(cb);
            access.noteFailure(Operation::ListEnd);
        }
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(PoolClearHook) {
    static u64 Callback(void* pool, std::uint32_t advance) {
        const auto caller = address(__builtin_return_address(0));
        const auto result = Orig(pool, advance);
        if (g_observing.load(std::memory_order_acquire) &&
            caller == g_mainBase + site::kModelPoolClearReturn && pool) {
            Access access;
            if (g_lifetime.hasPool(address(pool))) g_lifetime.clearPool(address(pool));
            access.noteFailure(Operation::PoolClear);
            if (!g_lifetime.failed()) g_bindingsAllowed.store(true, std::memory_order_release);
        }
        return result;
    }
};

HOOK_DEFINE_INLINE(ListCopyHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_bindingsAllowed.load(std::memory_order_acquire)) return;
        const auto* drawContext = reinterpret_cast<const void*>(ctx->X[19]);
        observeTransfer(drawContext ? read<std::uintptr_t>(drawContext, 0xB8) : 0,
                        ctx->X[8], Operation::ListCopy);
        Access access;
        ++g_modelCopies;
    }
};

HOOK_DEFINE_TRAMPOLINE(DisplayCallHook) {
    static void Callback(void* display, void* context, unsigned rebuild) {
        if (g_bindingsAllowed.load(std::memory_order_acquire) && display && (rebuild & 1u)) {
            Access access;
            const auto objects = read<std::uintptr_t>(display, 0x7B8);
            const auto count = read<std::uint32_t>(display, 0x7B0);
            if (objects && count) {
                if (g_lifetime.configurePool(address(display), objects, count, 120))
                    g_lifetime.clearPool(address(display));
                access.noteFailure(Operation::AliasReset);
            }
        }
        Orig(display, context, rebuild);
    }
};

HOOK_DEFINE_INLINE(ListAliasHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_bindingsAllowed.load(std::memory_order_acquire)) return;
        Access access;
        if (!g_lifetime.copyListObject(ctx->X[14], ctx->X[15]) && !g_failureOperation)
            g_transferFailure = {ctx->X[15], ctx->X[14], g_lifetime.listMask(ctx->X[14]), 0, 0};
        ++g_aliasCopies;
        access.noteFailure(Operation::AliasCopy);
    }
};

HOOK_DEFINE_INLINE(DisplayCopyHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_bindingsAllowed.load(std::memory_order_acquire)) return;
        const auto* context = reinterpret_cast<const void*>(ctx->X[22]);
        observeTransfer(context ? read<std::uintptr_t>(context, 0xB8) : 0,
                        ctx->X[0], Operation::DisplayCopy);
        Access access;
        ++g_displayCopies;
    }
};

HOOK_DEFINE_TRAMPOLINE(DirectCallHook) {
    static u64 Callback(void* list, void* context, unsigned flags) {
        std::uint8_t mask = 0;
        if (g_bindingsAllowed.load(std::memory_order_acquire)) {
            Access access;
            if (g_lifetime.listMask(address(list))) {
                const auto* slot = read<const void*>(reinterpret_cast<const void*>(g_mainBase), 0x462EF88);
                const auto* graphics = slot ? read<const void*>(slot, 0) : nullptr;
                if (!graphics || read<std::uintptr_t>(graphics, 0x38) != g_queue) g_lifetime.freeze();
                else mask = g_lifetime.beginDirect(address(list));
                if (mask) ++g_directSubmits;
            }
            access.noteFailure(Operation::DirectBegin);
        }
        const auto result = Orig(list, context, flags);
        if (mask) {
            Access access;
            g_lifetime.endDirect(mask);
            access.noteFailure(Operation::DirectEnd);
        }
        return result;
    }
};
} // namespace

Access::Access() {
    while (g_lock.test_and_set(std::memory_order_acquire)) asm volatile("yield");
}
Access::~Access() { g_lock.clear(std::memory_order_release); }
pure::RecallGpuLifetime& Access::ledger() const { return g_lifetime; }
unsigned Access::noteFailure(Operation operation) const {
    if (g_lifetime.failed() && !g_failureOperation)
        g_failureOperation = static_cast<unsigned>(operation);
    return g_failureOperation;
}
unsigned Access::failureOperation() const { return g_failureOperation; }
void Access::logBindingFailure(std::uintptr_t commandBuffer, unsigned slot) const {
    if (++g_bindRejections > 4 && g_bindRejections % 1800 != 0) return;
    Logging.Log("[self-recall] GPU_BIND_REJECTED cb=%p slot=%u root=%p recorder=%u list_seen=%llu list_tracked=%llu last_caller=%llx operation=%u",
        reinterpret_cast<void*>(commandBuffer), slot, reinterpret_cast<void*>(g_rootCommandBuffer),
        unsigned(g_lifetime.hasRecorder(commandBuffer)), static_cast<unsigned long long>(g_listBeginsSeen),
        static_cast<unsigned long long>(g_listBeginsTracked), static_cast<unsigned long long>(g_lastListCaller),
        g_failureOperation);
}

void requestTracking() { g_requested.store(true, std::memory_order_release); }
bool bindingPhaseReady() { return g_bindingsAllowed.load(std::memory_order_acquire); }

void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    FrameworkDrawHook::InstallAtOffset(0x00B592A4);
    FrameSealHook::InstallAtOffset(0x00B594B0);
    FrameworkPresentHook::InstallAtOffset(0x008C1470);
    FenceCallHook::InstallAtOffset(0x008C1504);
    WaitCallHook::InstallAtOffset(0x00B23DF8);
    WaitCallHook::InstallAtOffset(0x00B23F3C);
    ListBeginHook::InstallAtOffset(0x00818884);
    ListEndHook::InstallAtOffset(0x00818694);
    PoolClearHook::InstallAtOffset(0x00977564);
    ListCopyHook::InstallAtOffset(0x00A43DB8);
    DisplayCallHook::InstallAtOffset(0x00BC87BC);
    ListAliasHook::InstallAtOffset(0x00BC89F8);
    DisplayCopyHook::InstallAtOffset(0x00BC8A84);
    DirectCallHook::InstallAtOffset(0x00BC8DAC);
}
} // namespace self_recall::gpu_lifetime
