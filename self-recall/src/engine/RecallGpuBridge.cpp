#include "totk/engine/ReadGuard.hpp"
#include "RecallGraphicsEngine.hpp"
#include "GameProfiles.hpp"

#include <atomic>
#include <cstring>
#include <lib.hpp>
#include <nvn/nvn.h>

namespace self_recall::gpu_lifetime {
namespace {
struct HookSites {
    std::ptrdiff_t draw, seal, present, fence, waitFirst, waitSecond;
    std::ptrdiff_t listBegin, listEnd, poolClear, listCopy, listCopyFallback;
    std::ptrdiff_t displayCall, listAlias, displayCopy, directCall, batchDisplayCopy;
};
struct GpuProfile {
    HookSites hooks;
    std::array<std::ptrdiff_t, 8> listBeginCalls;
    std::ptrdiff_t poolClearReturn, graphicsSlot, queueManagerSlot;
    unsigned listCopyContextRegister, listCopyFallbackContextRegister;
    unsigned listCopyObjectRegister, displayCopyContextRegister;
    profiles::Site inlineListEnd;
};
constexpr GpuProfile kGpuProfiles[]{
    {{0xAE20DC, 0xAE22E8, 0x87FF20, 0x87FFB4, 0xA92038, 0xA9217C,
      0x858478, 0x8572B0, 0x93FFD8, 0xA1F618, 0,
      0xB988E8, 0xB98B24, 0xB98BB0, 0xB98DD4, 0},
     {0x856D48, 0x856E48, 0x857034, 0x857800, 0x857A74, 0x8581F4, 0x858354, 0x93879C},
     0x93FB14, 0x45586F0, 0, 19, 19, 8, 22, {}},
    {{0xB710B4, 0xB712C0, 0x89C4E4, 0x89C574, 0xB39AA0, 0xB39BE4,
      0x83FA60, 0x83F798, 0x944F2C, 0x95FBE4, 0,
      0xBE874C, 0xBE8988, 0xBE8A14, 0xBE8C38, 0},
     {0x83EA1C, 0x83EB1C, 0x83EF18, 0x83F09C, 0x83F420, 0x83F710, 0, 0x93D0B4},
     0x944A68, 0x4631878, 0, 26, 26, 8, 22, {}},
    {{0xB4A5F0, 0xB4A7FC, 0x8AAE10, 0x8AAEA4, 0xB0DE38, 0xB0DF7C,
      0x808930, 0x808668, 0x9160C4, 0x9316F4, 0,
      0xBCCB6C, 0xBCCDA8, 0xBCCE34, 0xBCD15C, 0},
     {0x8078F0, 0x8079F0, 0x807E3C, 0x807F70, 0x8082EC, 0x8085E0, 0, 0x90DFC8},
     0x915C00, 0x462BBE8, 0, 26, 26, 8, 22, {}},
    {{0xA704E4, 0xA706F0, 0x84BEDC, 0x84BF70, 0xA33148, 0xA3328C,
      0x7D25E0, 0x7D23F0, 0x9154F8, 0xA04BAC, 0,
      0xB4195C, 0xB41B98, 0xB41C24, 0xB41F4C, 0},
     {0x7D0F60, 0x7D1060, 0x7D123C, 0x7D1874, 0x7D1AFC, 0x7D2130, 0x7D2290, 0x90D440},
     0x915034, 0x461FF98, 0, 19, 19, 8, 22, {}},
    {{0xB592A4, 0xB594B0, 0x8C1470, 0x8C1504, 0xB23DF8, 0xB23F3C,
      0x818884, 0x818694, 0x977564, 0xA43DB8, 0,
      0xBC87BC, 0xBC89F8, 0xBC8A84, 0xBC8DAC, 0},
     {0x8174C8, 0x8175C8, 0x817A90, 0x817BD0, 0x818088, 0x818564, 0x819000, 0x96F38C},
     0x9770A0, 0x462EF88, 0, 19, 19, 8, 22, {}},
    {{0x3E11E4, 0x3E1374, 0x3F1704, 0x3F17A4, 0x3E1770, 0,
      0x183CF4, 0x1799D0, 0x1A5BC0, 0x1CC304, 0x5408C8,
      0x497970, 0x497C00, 0x497DAC, 0x27EBE70, 0x499920},
     {0x16C724, 0x16C9C8, 0x16CC5C, 0x187A64, 0x1883D0, 0, 0, 0},
     0x1A4984, 0x394FC38, 0x394FF70, 20, 20, 11, 20, {0x187FB0, 0xD63F0100}},
    {{0x38A644, 0x38A7D4, 0x396528, 0x3965C8, 0x38ABD0, 0,
      0x16A7D4, 0x1637D4, 0x1BB580, 0x1A860C, 0x39F114,
      0x494E40, 0x4950D0, 0x49527C, 0x27E0694, 0x495470},
     {0x1565A0, 0x156844, 0x156AD8, 0x16F35C, 0x16FCA0, 0, 0, 0},
     0x1BA344, 0x394AC38, 0x394AF70, 24, 25, 11, 20, {0x16F880, 0xD63F0100}},
    {{0x2D50E8, 0x2D5278, 0x2F0840, 0x2F08E0, 0x2D5674, 0,
      0xDF854, 0xD44F0, 0x101DA0, 0x1248EC, 0x2DAC1C,
      0x3DC1B0, 0x3DC440, 0x3DC5EC, 0x27E16A4, 0x3DC7E0},
     {0xC95FC, 0xC91F4, 0xC9488, 0xE4124, 0xE4A20, 0, 0, 0},
     0x100B64, 0x394CC38, 0x394CF70, 24, 25, 11, 20, {0xE46A0, 0xD63F0100}},
    {{0x2DB40C, 0x2DB59C, 0x2DC1E4, 0x2DC284, 0x2DB998, 0,
      0x90274, 0x8B934, 0xD5568, 0xF19EC, 0x7A9A60,
      0x3DCD90, 0x3DD020, 0x3DD1CC, 0x27F218C, 0x3DD3C0},
     {0x77EA0, 0x77A98, 0x77D2C, 0x97524, 0x97E08, 0, 0, 0},
     0xD4280, 0x395EC38, 0x395EF70, 24, 20, 11, 20, {0x97AA0, 0xD63F0100}},
};
const GpuProfile* g_profile = nullptr;
std::uintptr_t g_mainBase = 0;
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
pure::RecallGpuLifetime g_lifetime;
std::atomic<bool> g_requested{false}, g_observing{false}, g_bindingsAllowed{false};
std::uintptr_t g_framework = 0, g_graphics = 0, g_queue = 0, g_sync = 0;
unsigned g_failureOperation = 0;
std::uint64_t g_untrackedDraws = 0;
bool g_failureLogged = false, g_sealFailureLogged = false, g_listBeginFailureLogged = false;

void observeTransfer(std::uintptr_t cb, std::uintptr_t object, Operation operation) {
    Access access;
    (void)g_lifetime.callList(cb, object);
    access.noteFailure(operation);
}

template<class T> T read(const void* object, std::size_t offset) {
    if (!totk::engine::read_guard::admit(object, offset, sizeof(T))) return T{};
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof(result));
    return result;
}
std::uintptr_t address(const void* value) { return reinterpret_cast<std::uintptr_t>(value); }

bool newerRenderer() { return profiles::newerRenderer(); }
const void* graphics() {
    if (!g_profile || !g_mainBase) return nullptr;
    const auto* slot = read<const void*>(reinterpret_cast<const void*>(g_mainBase),
                                         g_profile->graphicsSlot);
    return slot ? read<const void*>(slot, 0) : nullptr;
}
std::uintptr_t directQueue(unsigned index) {
    if (!g_profile || !g_profile->queueManagerSlot || !g_mainBase || index >= 8) return 0;
    const auto* slot = read<const void*>(reinterpret_cast<const void*>(g_mainBase),
                                         g_profile->queueManagerSlot);
    const auto* manager = slot ? read<const void*>(slot, 0) : nullptr;
    if (!manager) return 0;
    const auto* entry = static_cast<const std::byte*>(manager) + 0x30 + 0x50 * index;
    return (read<std::uint16_t>(entry, 0x48) & 0x40)
        ? read<std::uintptr_t>(entry, 0) : 0;
}
std::uintptr_t frameQueue(const void* framework) {
    const auto* object = newerRenderer() ? graphics() : framework;
    return object ? read<std::uintptr_t>(object, newerRenderer() ? 0x68 : 0x1B0) : 0;
}
std::uintptr_t rootCommandBuffer(const void* framework) {
    const auto* object = newerRenderer() ? graphics() : framework;
    return object ? read<std::uintptr_t>(object, newerRenderer() ? 0x40 : 0x170) : 0;
}
std::uint64_t commandHandle(const void* framework) {
    const auto* object = newerRenderer() ? graphics() : framework;
    return object ? read<std::uint64_t>(object, newerRenderer() ? 0x60 : 0x1E8) : 0;
}
bool validSync(const void* framework, const void* sync) {
    if (!newerRenderer()) return address(sync) == g_sync &&
        read<std::uintptr_t>(framework, 0x1D8) == g_sync;
    const auto* object = graphics();
    if (!object || address(object) != g_graphics) return false;
    const auto index = read<std::uint32_t>(object, 0x15C);
    const auto selected = read<std::uintptr_t>(object, 0xB8 + 8 * (index < 2 ? index : 0));
    return selected && selected == address(sync);
}
bool isRenderListBegin(std::uintptr_t caller) {
    if (!g_profile || caller < g_mainBase) return false;
    for (const auto call : g_profile->listBeginCalls)
        if (call && caller - g_mainBase == static_cast<std::uintptr_t>(call + 4))
            return true;
    return false;
}
bool matchesFramework(const void* framework) {
    if (!framework || address(framework) != g_framework ||
        frameQueue(framework) != g_queue) return false;
    if (newerRenderer()) return address(graphics()) == g_graphics;
    return read<std::uintptr_t>(framework, 0x1D8) == g_sync;
}

HOOK_DEFINE_TRAMPOLINE(FrameworkDrawHook) {
    static u64 Callback(void* framework) {
        if (g_requested.load(std::memory_order_acquire) && framework) {
            const auto flags = read<std::uint8_t>(framework, newerRenderer() ? 0x1DA : 0x222);
            if (!(flags & 1u) || (flags & 2u)) {
                Access access;
                if (g_failureOperation && !g_failureLogged) {
                    g_failureLogged = true;
                    Logging.Log("[self-recall] GPU_LIFETIME_FAILED operation=%u", g_failureOperation);
                }
                const auto queue = frameQueue(framework);
                const auto sync = newerRenderer() ? 0 : read<std::uintptr_t>(framework, 0x1D8);
                const auto cb = rootCommandBuffer(framework);
                if (!g_framework && queue && (newerRenderer() || sync) && cb) {
                    g_framework = address(framework);
                    g_graphics = newerRenderer() ? address(graphics()) : 0;
                    g_queue = queue;
                    g_sync = sync;
                }
                if (!matchesFramework(framework)) g_lifetime.freeze();
                else g_lifetime.beginFrame(cb);
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
        const bool matched = matchesFramework(framework);
        const auto cb = matched ? rootCommandBuffer(framework) : 0;
        const bool recording = cb && g_lifetime.hasRecordingFrame(cb);
        const auto probe = g_lifetime.inspectSeal(cb, ctx->X[0]);
        if (!matched) g_lifetime.freeze();
        else g_lifetime.sealFrame(cb, ctx->X[0]);
        access.noteFailure(Operation::FrameSeal);
        if (g_lifetime.failed() && !g_sealFailureLogged) {
            g_sealFailureLogged = true;
            Logging.Log("[self-recall] GPU_SEAL_FAILED matched=%u recording=%u blocker=%u",
                        unsigned(matched), unsigned(recording), probe.blocker);
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(FrameworkPresentHook) {
    static u64 Callback(void* framework) {
        if (g_observing.load(std::memory_order_acquire) && framework &&
            read<std::uint8_t>(framework, newerRenderer() ? 0x1DB : 0x223)) {
            Access access;
            if (!matchesFramework(framework)) g_lifetime.freeze();
            else g_lifetime.beginSubmission(commandHandle(framework));
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
        if (!matchesFramework(framework) || address(queue) != g_queue ||
            !validSync(framework, sync))
            g_lifetime.freeze();
        if (newerRenderer() && validSync(framework, sync)) g_sync = address(sync);
        ticket = g_lifetime.captureDirectFence();
        access.noteFailure(Operation::FencePrepare);
    }
    original(queue, sync, condition, flags);
    {
        Access access;
        if (matchesFramework(framework))
            g_lifetime.submittedAndFenced(commandHandle(framework), g_queue, address(sync), ticket);
        else g_lifetime.freeze();
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
        (void)g_lifetime.completeWait(ticket, result);
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
            if (!isRenderListBegin(caller)) return result;
            Access access;
            const auto objects = read<std::uintptr_t>(pool, newerRenderer() ? 0xD8 : 0x120);
            const auto count = read<std::uint32_t>(pool, newerRenderer() ? 0xD0 : 0x118);
            const auto cb = read<std::uintptr_t>(drawContext, 0xB8);
            const auto stride = newerRenderer() ? 152u : 120u;
            if (g_lifetime.configurePool(address(pool), objects, count, stride)) {
                const bool priorRecorder = g_lifetime.hasRecorder(cb);
                const bool priorFrame = g_lifetime.hasRecordingFrame(cb);
                if (!g_lifetime.beginList(cb, address(pool)) && !g_listBeginFailureLogged) {
                    g_listBeginFailureLogged = true;
                    Logging.Log("[self-recall] GPU_LIST_BEGIN_FAILED caller=%lx prior_recorder=%u prior_frame=%u",
                        caller - g_mainBase, unsigned(priorRecorder), unsigned(priorFrame));
                }
                access.noteFailure(Operation::ListBegin);
            } else access.noteFailure(Operation::PoolConfigure);
        }
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ListEndHook) {
    static u64 Callback(void* pool, std::uintptr_t arg1, std::uint32_t arg2) {
        std::uintptr_t cb = 0;
        if (g_bindingsAllowed.load(std::memory_order_acquire) && pool) {
            if (newerRenderer() && arg1) {
                cb = read<std::uintptr_t>(reinterpret_cast<const void*>(arg1), 0xB8);
            } else if (!newerRenderer()) {
                const auto index = static_cast<std::uint32_t>(arg1);
                const auto count = read<std::uint32_t>(pool, 0x118);
                const auto* objects = read<const std::byte*>(pool, 0x120);
                if (objects && count && index < count) {
                    const auto* context = read<const void*>(objects + std::size_t{index} * 120, 0x30);
                    if (context) cb = read<std::uintptr_t>(context, 0xB8);
                }
            }
        }
        const auto result = Orig(pool, arg1, arg2);
        if (cb) {
            Access access;
            if (g_lifetime.hasRecorder(cb)) (void)g_lifetime.endList(cb);
            access.noteFailure(Operation::ListEnd);
        }
        return result;
    }
};

using NativeListEnd = u64 (*)(void*);
u64 endListWithReceipt(void* commandBuffer, NativeListEnd original) {
    const auto result = original(commandBuffer);
    if (g_bindingsAllowed.load(std::memory_order_acquire)) {
        Access access;
        const auto cb = address(commandBuffer);
        if (g_lifetime.hasRecorder(cb)) (void)g_lifetime.endList(cb);
        access.noteFailure(Operation::ListEnd);
    }
    return result;
}

HOOK_DEFINE_INLINE(InlineListEndHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_bindingsAllowed.load(std::memory_order_acquire)) return;
        ctx->X[1] = ctx->X[8];
        ctx->X[8] = reinterpret_cast<std::uintptr_t>(&endListWithReceipt);
    }
};

HOOK_DEFINE_TRAMPOLINE(PoolClearHook) {
    static u64 Callback(void* pool, std::uint32_t advance) {
        const auto caller = address(__builtin_return_address(0));
        const auto result = Orig(pool, advance);
        if (g_observing.load(std::memory_order_acquire) &&
            g_profile && caller == g_mainBase + g_profile->poolClearReturn && pool) {
            Access access;
            if (g_lifetime.hasPool(address(pool))) g_lifetime.clearPool(address(pool));
            access.noteFailure(Operation::PoolClear);
            if (!g_lifetime.failed()) g_bindingsAllowed.store(true, std::memory_order_release);
        }
        return result;
    }
};

void recordModelListCopy(exl::hook::InlineCtx* ctx, bool fallback) {
    if (!g_bindingsAllowed.load(std::memory_order_acquire)) return;
    const auto* drawContext = reinterpret_cast<const void*>(
        ctx->X[fallback ? g_profile->listCopyFallbackContextRegister
                        : g_profile->listCopyContextRegister]);
    const auto commandBuffer = drawContext ? read<std::uintptr_t>(drawContext, 0xB8) : 0;
    const auto object = ctx->X[g_profile->listCopyObjectRegister];
    observeTransfer(commandBuffer, object, Operation::ListCopy);
}
HOOK_DEFINE_INLINE(ListCopyHook) {
    static void Callback(exl::hook::InlineCtx* ctx) { recordModelListCopy(ctx, false); }
};
HOOK_DEFINE_INLINE(ListCopyFallbackHook) {
    static void Callback(exl::hook::InlineCtx* ctx) { recordModelListCopy(ctx, true); }
};

HOOK_DEFINE_TRAMPOLINE(DisplayCallHook) {
    static u64 Callback(void* display, void* context, unsigned rebuild) {
        if (g_bindingsAllowed.load(std::memory_order_acquire) && display && (rebuild & 1u)) {
            Access access;
            const auto objects = read<std::uintptr_t>(display, newerRenderer() ? 0x878 : 0x7B8);
            const auto count = read<std::uint32_t>(display, newerRenderer() ? 0x870 : 0x7B0);
            if (objects && count) {
                if (g_lifetime.configurePool(address(display), objects, count,
                                             newerRenderer() ? 152 : 120))
                    g_lifetime.clearPool(address(display));
                access.noteFailure(Operation::AliasReset);
            }
        }
        return Orig(display, context, rebuild);
    }
};

HOOK_DEFINE_INLINE(ListAliasHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_bindingsAllowed.load(std::memory_order_acquire)) return;
        Access access;
        (void)g_lifetime.copyListObject(ctx->X[14], ctx->X[15]);
        access.noteFailure(Operation::AliasCopy);
    }
};

HOOK_DEFINE_INLINE(DisplayCopyHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        if (!g_bindingsAllowed.load(std::memory_order_acquire)) return;
        const auto* context = reinterpret_cast<const void*>(
            ctx->X[g_profile->displayCopyContextRegister]);
        observeTransfer(context ? read<std::uintptr_t>(context, 0xB8) : 0,
                        ctx->X[0], Operation::DisplayCopy);
    }
};

HOOK_DEFINE_TRAMPOLINE(DirectCallHook) {
    static u64 Callback(void* list, void* context, unsigned flags) {
        std::uint8_t mask = 0;
        if (g_bindingsAllowed.load(std::memory_order_acquire)) {
            Access access;
            if (g_lifetime.listMask(address(list))) {
                const auto* nativeGraphics = graphics();
                if (!nativeGraphics ||
                    read<std::uintptr_t>(nativeGraphics, newerRenderer() ? 0x68 : 0x38) != g_queue)
                    g_lifetime.freeze();
                else mask = g_lifetime.beginDirect(address(list));
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
HOOK_DEFINE_TRAMPOLINE(BatchDisplayCopyHook) {
    static u64 Callback(void* context, const std::uintptr_t* lists, int count) {
        if (g_bindingsAllowed.load(std::memory_order_acquire) && context && lists &&
            count > 0 && count <= 255) {
            const auto cb = read<std::uintptr_t>(context, 0xB8);
            for (int i = 0; i < count; ++i) {
                const auto object = lists[i];
                if (!object || !(read<std::uint8_t>(reinterpret_cast<const void*>(object), 0x58) & 1u))
                    continue;
                observeTransfer(cb, object, Operation::DisplayCopy);
            }
        }
        return Orig(context, lists, count);
    }
};
HOOK_DEFINE_TRAMPOLINE(BatchDirectCallHook) {
    static u64 Callback(unsigned queueIndex, const std::uintptr_t* lists,
                        int count, unsigned flags) {
        unsigned active = 0;
        if (g_bindingsAllowed.load(std::memory_order_acquire) && lists && count > 0 && count <= 255) {
            Access access;
            const auto queue = directQueue(queueIndex);
            for (int i = 0; i < count; ++i) {
                const auto object = lists[i];
                if (!object || !(read<std::uint8_t>(reinterpret_cast<const void*>(object), 0x58) & 1u) ||
                    !g_lifetime.listMask(object) || !queue) continue;
                if (queue != g_queue) {
                    g_lifetime.freeze();
                    break;
                }
                if (g_lifetime.beginDirect(object)) ++active;
            }
            access.noteFailure(Operation::DirectBegin);
        }
        const auto result = Orig(queueIndex, lists, count, flags);
        if (active) {
            Access access;
            while (active--) g_lifetime.endDirect(1);
            access.noteFailure(Operation::DirectEnd);
        }
        return result;
    }
};
}

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
void Access::logUntrackedDraw(std::uintptr_t caller) const {
    if (++g_untrackedDraws <= 4)
        Logging.Log("[self-recall] PALETTE_UNTRACKED_DRAW caller=%lx count=%llu", caller - g_mainBase,
                    static_cast<unsigned long long>(g_untrackedDraws));
}

void requestTracking() { g_requested.store(true, std::memory_order_release); }
bool bindingPhaseReady() { return g_bindingsAllowed.load(std::memory_order_acquire); }

void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    g_profile = &profiles::row(kGpuProfiles);
    const auto inlineEnd = g_profile->inlineListEnd;
    if (inlineEnd.offset) {
        const auto* entry = reinterpret_cast<const void*>(mainBase + inlineEnd.offset - 8);
        if (read<std::uint32_t>(entry, 0) != 0xAA1603E0 ||
            read<std::uint32_t>(entry, 4) != 0xF9400108 ||
            read<std::uint32_t>(entry, 8) != inlineEnd.word ||
            read<std::uint32_t>(entry, 12) != 0xF9002B00) {
            Access access;
            g_lifetime.freeze();
            access.noteFailure(Operation::ListEnd);
            Logging.Log("[self-recall] GPU_PROFILE_FAILED inline_list_end=%llx",
                        static_cast<unsigned long long>(inlineEnd.offset));
            return;
        }
    }
    const auto& site = g_profile->hooks;
    if (site.draw) FrameworkDrawHook::InstallAtOffset(site.draw);
    if (site.seal) FrameSealHook::InstallAtOffset(site.seal);
    if (site.present) FrameworkPresentHook::InstallAtOffset(site.present);
    if (site.fence) FenceCallHook::InstallAtOffset(site.fence);
    if (site.waitFirst) WaitCallHook::InstallAtOffset(site.waitFirst);
    if (site.waitSecond) WaitCallHook::InstallAtOffset(site.waitSecond);
    if (site.listBegin) ListBeginHook::InstallAtOffset(site.listBegin);
    if (site.listEnd) ListEndHook::InstallAtOffset(site.listEnd);
    if (inlineEnd.offset) InlineListEndHook::InstallAtOffset(inlineEnd.offset);
    if (site.poolClear) PoolClearHook::InstallAtOffset(site.poolClear);
    if (site.listCopy) ListCopyHook::InstallAtOffset(site.listCopy);
    if (site.listCopyFallback) ListCopyFallbackHook::InstallAtOffset(site.listCopyFallback);
    if (site.displayCall) DisplayCallHook::InstallAtOffset(site.displayCall);
    if (site.listAlias) ListAliasHook::InstallAtOffset(site.listAlias);
    if (site.displayCopy) DisplayCopyHook::InstallAtOffset(site.displayCopy);
    if (site.batchDisplayCopy) BatchDisplayCopyHook::InstallAtOffset(site.batchDisplayCopy);
    if (site.directCall) {
        if (newerRenderer()) BatchDirectCallHook::InstallAtOffset(site.directCall);
        else DirectCallHook::InstallAtOffset(site.directCall);
    }
}
}

namespace self_recall::palette {
namespace {
static_assert(sizeof(NVNbufferBuilder) == 0x40);
static_assert(sizeof(NVNbuffer) == 0x30);
static_assert(sizeof(NVNmemoryPoolBuilder) == 0x40);
static_assert(sizeof(NVNmemoryPool) == 0x100);
static_assert(sizeof(NVNboolean) == 1);
static_assert(NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED == 2 && NVN_MEMORY_POOL_FLAGS_CPU_CACHED == 4);
static_assert(NVN_MEMORY_POOL_FLAGS_GPU_UNCACHED == 0x10 && NVN_MEMORY_POOL_FLAGS_GPU_CACHED == 0x20);

template<class T> T read(std::uintptr_t address) {
    if (!totk::engine::read_guard::admit(reinterpret_cast<const void*>(address), 0, sizeof(T))) return T{};
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}
template<class T> T dispatch(std::uintptr_t base, std::uintptr_t offset) {
    const auto slot = read<std::uintptr_t>(base + offset);
    return slot ? read<T>(slot) : T{};
}
struct GpuApiSites {
    std::uintptr_t graphicsSlot, getProcVariable, policySlot, deviceOffset;
};
constexpr GpuApiSites kGpuApiSites[]{
    {0x45586F0, 0x4597DC8, 0x4561D28, 0x30},
    {0x4631878, 0x4673088, 0x463AE00, 0x30},
    {0x462BBE8, 0x466D408, 0x4635188, 0x30},
    {0x461FF98, 0x46617A8, 0x4629530, 0x30},
    {0x462EF88, 0x46707E8, 0x4638520, 0x30},
    {0x394FC38, 0x3A16EC0, 0, 0x168},
    {0x394AC38, 0x3A11EC0, 0, 0x168},
    {0x394CC38, 0x3A13EC0, 0, 0x168},
    {0x395EC38, 0x3A25EC0, 0, 0x168},
};
template<class F> F resolve(PFNNVNDEVICEGETPROCADDRESSPROC getProc,
                            const NVNdevice* device, const char* name) {
    return reinterpret_cast<F>(getProc(device, name));
}
}

GpuBufferApi resolveGpuBufferApi(std::uintptr_t mainBase) {
    GpuBufferApi api;
    if (!mainBase) return api;
    const auto& sites = profiles::row(kGpuApiSites);
    const auto graphics = dispatch<std::uintptr_t>(mainBase, sites.graphicsSlot);
    const auto getProc = read<PFNNVNDEVICEGETPROCADDRESSPROC>(mainBase + sites.getProcVariable);
    if (!graphics || !getProc) return api;
    api.device = read<void*>(graphics + sites.deviceOffset);
    if (!api.device) return {};
    const auto* device = static_cast<const NVNdevice*>(api.device);
    if (sites.policySlot) {
        const auto policy = dispatch<std::uintptr_t>(mainBase, sites.policySlot);
        if (!policy) return {};
        api.memoryPoolFlags = uniformPoolFlags(read<std::uint32_t>(policy + 0xC),
                                               read<std::uint32_t>(policy + 0x10));
    } else {
        api.memoryPoolFlags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED;
    }
    api.poolBuilderDefaults = resolve<decltype(api.poolBuilderDefaults)>(getProc, device, "nvnMemoryPoolBuilderSetDefaults");
    api.poolBuilderDevice = resolve<decltype(api.poolBuilderDevice)>(getProc, device, "nvnMemoryPoolBuilderSetDevice");
    api.poolBuilderStorage = resolve<decltype(api.poolBuilderStorage)>(getProc, device, "nvnMemoryPoolBuilderSetStorage");
    api.poolBuilderFlags = resolve<decltype(api.poolBuilderFlags)>(getProc, device, "nvnMemoryPoolBuilderSetFlags");
    api.poolInitialize = resolve<decltype(api.poolInitialize)>(getProc, device, "nvnMemoryPoolInitialize");
    api.poolFinalize = resolve<decltype(api.poolFinalize)>(getProc, device, "nvnMemoryPoolFinalize");
    api.poolFlags = resolve<decltype(api.poolFlags)>(getProc, device, "nvnMemoryPoolGetFlags");
    api.builderDefaults = resolve<decltype(api.builderDefaults)>(getProc, device, "nvnBufferBuilderSetDefaults");
    api.builderDevice = resolve<decltype(api.builderDevice)>(getProc, device, "nvnBufferBuilderSetDevice");
    api.builderStorage = resolve<decltype(api.builderStorage)>(getProc, device, "nvnBufferBuilderSetStorage");
    api.initialize = resolve<decltype(api.initialize)>(getProc, device, "nvnBufferInitialize");
    api.finalize = resolve<decltype(api.finalize)>(getProc, device, "nvnBufferFinalize");
    api.map = resolve<decltype(api.map)>(getProc, device, "nvnBufferMap");
    api.address = resolve<decltype(api.address)>(getProc, device, "nvnBufferGetAddress");
    api.flush = resolve<decltype(api.flush)>(getProc, device, "nvnMemoryPoolFlushMappedRange");
    return api ? api : GpuBufferApi{};
}
}
