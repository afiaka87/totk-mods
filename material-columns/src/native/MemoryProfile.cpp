// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "MemoryProfile.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <array>
#include <atomic>
#include <lib.hpp>

namespace material_columns::native::memory {
namespace {
std::uintptr_t base{};
bool installed{};
// No allocator interception or per-frame log stream. The registry contains borrowed
// identities and counters only; it never owns or prolongs a native resource's lifetime.
struct Capture {
    std::atomic<std::uintptr_t> owner{}, pane{};
    std::atomic<std::uint64_t> output{}, scratch{}, extraScratch{};
    std::atomic<std::uintptr_t> outputTexture{}, workTexture{};
    std::atomic<bool> enabled{};
};
std::array<Capture, 16> captures{};
std::atomic<unsigned> captureLogs{};
std::atomic<std::uintptr_t> bindingThread{};
std::atomic<unsigned> privateLoads{}, stockLoads{}, sharedLoads{};
std::atomic<std::uint64_t> fadeHigh{};
std::atomic<unsigned> releases{}, activeTextures{};

template<class T> T field(std::uintptr_t object, unsigned offset) {
    return *reinterpret_cast<const T*>(object + offset);
}
std::uintptr_t threadId() {
#ifdef MATERIAL_COLUMNS_MEMORY_HOST_TEST
    return 1;
#else
    std::uintptr_t value;
    asm volatile("mrs %0, tpidrro_el0" : "=r"(value));
    return value;
#endif
}
Capture* find(std::uintptr_t pane) {
    for (auto& capture : captures) if (capture.pane.load(std::memory_order_acquire) == pane) return &capture;
    return nullptr;
}
std::uint64_t textureBytes(std::uintptr_t texture) {
    // TextureData +64 image storage plus +76 auxiliary storage, as used by the
    // native DynamicTextureAllocator dump. Includes native texture alignment.
    return texture ? field<std::uint64_t>(texture, 64) + field<std::uint32_t>(texture, 76) : 0;
}
void maximum(std::atomic<std::uint64_t>& target, std::uint64_t value) {
    auto previous = target.load(std::memory_order_relaxed);
    while (previous < value && !target.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {}
}
// Run before the native TST restores condition flags (InlineCtx saves no NZCV). Later MOV W4,W28 / MOV W5,W23 supply the allocator dimensions. Only integer math: InlineCtx does not preserve live SIMD registers. The native world rectangle in S10/S9 stays unchanged; W28/W23 control target resolution.
HOOK_DEFINE_INLINE(ScaleCaptureTarget) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        const auto pane = field<std::uintptr_t>(ctx->X[29] - 24, 0);
        const auto* entry = find(pane);
        if (!entry || !entry->enabled.load(std::memory_order_acquire)) return;
        for (unsigned r : {28u, 23u}) {
            const auto pixels = static_cast<std::uint32_t>(ctx->X[r]);
            ctx->X[r] = pixels < 3 ? 1 : (std::uint64_t(pixels) * 2) / 5;
        }
    }
};
HOOK_DEFINE_TRAMPOLINE(CaptureWork) {
    static std::uintptr_t Callback(std::uintptr_t pane, std::uintptr_t draw, std::uintptr_t flags,
        std::uintptr_t filter, std::uintptr_t buffer, std::uintptr_t target,
        std::uintptr_t color, std::uintptr_t device) {
        const auto result = Orig(pane, draw, flags, filter, buffer, target, color, device);
        if (auto* entry = find(pane); entry && entry->enabled.load(std::memory_order_acquire)) {
            maximum(entry->scratch, textureBytes(result));
            if (result && !entry->workTexture.exchange(result,std::memory_order_relaxed)) ++activeTextures;
        }
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(DynamicCaptureDraw) {
    static std::uintptr_t Callback(std::uintptr_t pane, std::uintptr_t draw, std::uintptr_t device) {
        const auto result = Orig(pane, draw, device);
        auto* entry = find(pane);
        if (!entry || !entry->enabled.load(std::memory_order_acquire) || !draw || !field<std::uintptr_t>(draw, 416) || !(field<unsigned char>(pane, 88) & 1) || !field<unsigned char>(pane, 89)) return result;
        const auto texture = field<std::uintptr_t>(pane, 280);
        if (!texture) return result;
        const auto bytes = textureBytes(texture);
        // Direct capture returns the work texture itself. Transfer its tracked
        // identity instead of charging/releasing the same allocation twice.
        const auto work = entry->workTexture.load(std::memory_order_relaxed);
        const bool direct = work == texture;
        if (direct) {
            entry->workTexture.store(0,std::memory_order_relaxed);
            --activeTextures;
        }
        const auto old = entry->output.exchange(bytes, std::memory_order_relaxed);
        if (!entry->outputTexture.exchange(texture,std::memory_order_relaxed)) ++activeTextures;
        if (!direct) maximum(entry->extraScratch, entry->scratch.load(std::memory_order_relaxed));
        std::uint64_t outputs{}, scratch{};
        unsigned seen{};
        for (const auto& capture : captures) if (capture.pane.load(std::memory_order_acquire)) {
            const auto output = capture.output.load(std::memory_order_relaxed);
            outputs += output;
            seen += output != 0;
            const auto extra = capture.extraScratch.load(std::memory_order_relaxed);
            if (extra > scratch) scratch = extra;
        }
        maximum(fadeHigh, outputs + scratch);
        if (old != bytes && captureLogs.fetch_add(1, std::memory_order_relaxed) < 104) {
            Logging.Log("[material-columns] memory fade pane=%llx texture=%llx dimensions=%ux%u aligned=%llu direct=%u seen=%u retained-envelope=%llu extra-scratch=%llu envelope-high=%llu",
                (unsigned long long)pane, (unsigned long long)texture,
                unsigned(field<std::uint16_t>(texture,48)), unsigned(field<std::uint16_t>(texture,50)),
                (unsigned long long)bytes, unsigned(direct), seen, (unsigned long long)outputs,
                (unsigned long long)scratch, (unsigned long long)fadeHigh.load());
        }
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(ReleaseTexture) {
    static std::uintptr_t Callback(std::uintptr_t allocator, std::uintptr_t texture) {
        // Observe native release without changing it. Dynamic textures return to a
        // shared pool; this says nothing about that pool's physical reservation.
        if (texture && activeTextures.load(std::memory_order_relaxed)) for (auto& entry : captures) {
            auto expected=texture;
            if (entry.outputTexture.compare_exchange_strong(expected,0,std::memory_order_relaxed)) { ++releases; --activeTextures; }
            expected=texture;
            if (entry.workTexture.compare_exchange_strong(expected,0,std::memory_order_relaxed)) { ++releases; --activeTextures; }
        }
        return Orig(allocator,texture);
    }
};
HOOK_DEFINE_TRAMPOLINE(LoadIcon) {
    static std::uintptr_t Callback(std::uintptr_t screen, std::uintptr_t path) {
        const bool isPrivate = bindingThread.load(std::memory_order_relaxed) == threadId();
        const auto result = Orig(screen, path);
        if (!result || field<unsigned>(screen, 364) != 0) return result;
        const auto binder = screen + 128 + 40 * field<unsigned char>(screen, 208);
        const auto resource = reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(base+0xb51804)(binder);
        const auto unit = field<std::uintptr_t>(binder, 8);
        if (!resource || !unit) return result;
        const auto refs = field<unsigned>(unit,136);
        const auto sequence = isPrivate ? privateLoads.fetch_add(1) : stockLoads.fetch_add(1);
        if (refs > 1) ++sharedLoads;
        // Separate quotas ensure native startup loads cannot consume the column samples.
        // At most 224 lines over the process lifetime, independent of inventory length.
        if (sequence >= (isPrivate ? 64u : 32u) && (sequence >= 4096 || sequence % 64)) return result;
        unsigned hash=2166136261u;
        const auto* name = reinterpret_cast<const char*>(field<std::uintptr_t>(path,0));
        if (name) for (unsigned i=0; i<160 && name[i]; ++i) hash=(hash^static_cast<unsigned char>(name[i]))*16777619u;
        const auto manager=field<std::uintptr_t>(unit,16);
        Logging.Log("[material-columns] memory icon private=%u path=%08x resource=%llx texture-info=%llx refs=%u binder-status=%u resource-bytes=%u cache-manager=%llx cache-total=%u private-loads=%u native-loads=%u shared-loads=%u",
            unsigned(isPrivate),hash,(unsigned long long)resource,(unsigned long long)(resource+56),refs,
            field<unsigned>(binder,4),field<unsigned>(resource,16),(unsigned long long)manager,
            manager ? field<unsigned>(manager,160) : 0,privateLoads.load(),stockLoads.load(),sharedLoads.load());
        return result;
    }
};
std::uint64_t freeBytes(std::uintptr_t heap) {
    return heap ? reinterpret_cast<std::uint64_t (*)(std::uintptr_t)>(field<std::uintptr_t>(field<std::uintptr_t>(heap,0),128))(heap) : 0;
}
} // namespace
HeapSample::HeapSample(std::uintptr_t value, const char* label, bool subinterval) : heap(value), phase(label), before(freeBytes(value)), detail(subinterval) {}
HeapSample::~HeapSample() {
    static unsigned remaining=12;
    if (!remaining) return;
    --remaining;
    const auto after=freeBytes(heap);
    Logging.Log("[material-columns] memory UI%s phase=%s heap=%llx free-before=%llu free-after=%llu interval-used=%lld",
        detail ? "-detail" : "",phase,(unsigned long long)heap,(unsigned long long)before,(unsigned long long)after,
        (long long)before-(long long)after);
}
void menuClosed() {
    static unsigned remaining=12;
    if (!remaining) return;
    --remaining;
    std::uint64_t logicalLive{};
    for (const auto& entry : captures) {
        if (entry.outputTexture.load(std::memory_order_relaxed)) logicalLive+=entry.output.load(std::memory_order_relaxed);
        if (entry.workTexture.load(std::memory_order_relaxed)) logicalLive+=entry.scratch.load(std::memory_order_relaxed);
    }
    Logging.Log("[material-columns] memory close logical-live-fades=%llu observed-releases=%u envelope-high=%llu private-loads=%u native-loads=%u shared-loads=%u",
        (unsigned long long)logicalLive,releases.load(),(unsigned long long)fadeHigh.load(),
        privateLoads.load(),stockLoads.load(),sharedLoads.load());
}
bool privateBinding(bool value) {
    const auto thread=threadId();
    const auto before=bindingThread.exchange(value ? thread : 0,std::memory_order_relaxed);
    return before==thread;
}
void registerCapture(std::uintptr_t owner, std::uintptr_t pane) {
    if (!pane || find(pane)) return;
    for (auto& entry : captures) if (!entry.pane.load(std::memory_order_relaxed)) {
        entry.owner.store(owner,std::memory_order_relaxed);
        entry.output.store(0,std::memory_order_relaxed);
        entry.scratch.store(0,std::memory_order_relaxed);
        entry.extraScratch.store(0,std::memory_order_relaxed);
        entry.outputTexture.store(0,std::memory_order_relaxed);
        entry.workTexture.store(0,std::memory_order_relaxed);
        entry.enabled.store(false,std::memory_order_relaxed);
        entry.pane.store(pane,std::memory_order_release);
        return;
    }
}
void captureEnabled(std::uintptr_t pane, bool enabled) {
    if (auto* entry = find(pane)) entry->enabled.store(enabled,std::memory_order_release);
}
void forget(std::uintptr_t owner) {
    for (auto& entry : captures) if (entry.owner.load(std::memory_order_relaxed)==owner) {
        entry.pane.store(0,std::memory_order_release);
        if (entry.outputTexture.exchange(0,std::memory_order_relaxed)) --activeTextures;
        if (entry.workTexture.exchange(0,std::memory_order_relaxed)) --activeTextures;
    }
}
bool install(std::uintptr_t mainBase, bool validateOnly) {
    if (installed) return true;
    constexpr totk::engine::CodeSite sites[] = {
        {0x943400,0x6db923e9,0xa9017bfd},
        {0x943f94,0xfc180fea,0x6d0123e9},
        {0x1ab3f2c,0xd10403ff,0xa90d7bfd},
        {0x7ef80c,0xd10143ff,0xa9027bfd},
        {0x944030,0xa9039be7,0x9001e7a9}
    };
    if (!totk::engine::codeMatches(mainBase,sites,"material-columns","MEMORY_HOOK_REFUSED")) return false;
    if (validateOnly) return true;
    base=mainBase;
    DynamicCaptureDraw::InstallAtOffset(sites[0].offset);
    CaptureWork::InstallAtOffset(sites[1].offset);
    LoadIcon::InstallAtOffset(sites[2].offset);
    ReleaseTexture::InstallAtOffset(sites[3].offset);
    ScaleCaptureTarget::InstallAtOffset(sites[4].offset);
    installed=true;
    Logging.Log("[material-columns] memory diagnostics registry=%u bytes; four shared bands include two borrowed native sides; direct work/output deduplicated; fade-envelope excludes native pool capacity; UI intervals include six pooled tile clones; UI-detail is a nested subset, not additive; UI intervals exclude parent-layout creation; icon cache-total is shared",
        unsigned(sizeof(captures)));
    return true;
}
} // namespace material_columns::native::memory
