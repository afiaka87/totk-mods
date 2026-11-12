// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "StasisSurface.hpp"
#include "StasisRender.hpp"
#include "StasisRenderRoute.hpp"
#include "StasisSignals.hpp"
#include "StasisActors.hpp"
#include "StasisModelView.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <lib.hpp>
#include <atomic>
#include <cstring>
namespace stasis::surface {
namespace {
using engine::call;
using namespace totk::port::literals;
// Members at a different offset on 1.4.3: {1.2.1, 1.4.3}.
constexpr totk::port::Member kMaskTexture{344, 352}, kSecondMaskTexture{352, 360};
constexpr totk::port::Member kNativeRequest{372, 380};
constexpr totk::port::Member kDrawUnitList{456, 360};
// 1.4.3 keeps the extra-model preparation as its own routine; its one caller is 0x9A5C4C.
constexpr std::ptrdiff_t kPrepareExtraModels143 = 0x1EEAAE4_v143;
constexpr std::ptrdiff_t kHighlightBit143 = 0x1EE39E4_v143;
constexpr totk::engine::CodeSite kHighlight143[] = {
    {0x1EEAAE4_v143, 0xA9BA7BFD, 0xF9000BFC}, // entry
    {0x1EEAB18_v143, 0x9B090697, 0xF84146EA}, // 72-byte entries, model at +0
    {0x1EEAB84_v143, 0xB9017809, 0},          // str w9, [x0, #376]
    {0x1EEABEC_v143, 0xBD400EE0, 0},          // position read at entry+32
    {0x9A5C4C_v143, 0x945513A6, 0},           // bl 0x1EEAAE4 in the special-power update
    {0x9A2E5C_v143, 0xB92BCEBF, 0x1280001C},  // str wzr, [x21, #11212]; mov w28, #-1
    {0x9A2E64_v143, 0xF94036A9, 0xB9002D3F},  // queue = [x21, #104]; str wzr, [x9, #44]
    {0x9A2E6C_v143, 0xA9017D3C, 0xB900413F},  // stp x28, xzr, [x9, #16]; str wzr, [x9, #64]
    {0x9A2E74_v143, 0xB9403AB8, 0},           // ring count [x21, #56]
    {0x9A2EA0_v143, 0xF80342BF, 0xA9007EDF},  // clear +52, then the four slots
    {0x9A5084_v143, 0x294622A9, 0},           // ring capacity +48, head +52
    {0x9A5090_v143, 0xF94016AB, 0},           // ring entries [x21, #40]
    {0x9A50A4_v143, 0x5280A909, 0},           // ring stride 0x548
    {0x9A50B8_v143, 0x39490509, 0x37000729},  // skip a model with byte 577 bit 0
    {0x9A50C0_v143, 0xB9402108, 0},           // model unit count +32
    {0x9A50E4_v143, 0xF9401508, 0xF8697908},  // model units +40
    {0x9A50EC_v143, 0xF9400116, 0},           // unit = [entry]
    {0x9A50FC_v143, 0xF9406508, 0},           // record count, vtable +200
    {0x9A511C_v143, 0xF9401AC8, 0},           // unit state [x22, #48]
    {0x9A512C_v143, 0xF9400509, 0},           // records [state, #8], 20 bytes each
    {0x9A5134_v143, 0x79401D2B, 0},           // ldrh record +14
    {0x9A5144_v143, 0x79001D2A, 0x39408509},  // strh; ldrb state +33
    {0x9A515C_v143, 0xB9400509, 0x321F0129},  // record +4 |= 2
    {0x9A5174_v143, 0xF9413108, 0},           // vtable +608 (unit, 1)
    {0x9A51C0_v143, 0x39490709, 0},           // first slot walk
    {0x1EE39E4_v143, 0x52800040, 0xD65F03C0}, // returns 2
};
struct State {
    std::uintptr_t owner{};
    totk::engine::ActorHandle actor{};
    totk::core::SceneToken scene{};
    unsigned epoch{}, nativeRequest{};
    void* hoverDepth{};
    bool active{}, masked{}, preview{};
};
Shared<State> g_state;
// Four units per roster actor on average; overflow refuses and logs the whole pass.
constexpr unsigned kUnitCapacity = engine::kRosterCapacity * 4;
std::uintptr_t g_units[kUnitCapacity]{};
ModelView g_view{};
ModelView g_hoverView{};
std::uintptr_t g_drawEntries[kUnitCapacity]{};
std::uint64_t g_queueMatches[kQueueNodeCapacity / 64]{};
struct ModelOwner {
    void* model{};
    unsigned first{}, count{};
};
ModelOwner g_models[engine::kRosterCapacity]{};
// Snapshot entries are compacted in place; accepted entries never overtake unread ones.
totk::engine::ActorHandle g_modelActors[engine::kRosterCapacity]{};
unsigned g_modelCount{};
std::atomic<unsigned> g_refused{};
std::atomic<bool> g_prepared143{}, g_drawn143{};
unsigned g_epoch{};
void markMask(unsigned epoch, bool valid) {
    g_state.update([&](State &state) {
        if (state.epoch == epoch)
            state.masked = valid;
    });
}
void refuse(unsigned reason, std::uintptr_t actor = 0, unsigned units = 0) {
    if (!(g_refused.fetch_or(1u << reason) & (1u << reason)))
        Logging.Log("[stasis] SURFACE_REFUSED reason=%u actor=%llx units=%u", reason, actor, units);
}
template <class T> T read(const void *p, std::size_t offset) {
    T out{};
    const auto address = reinterpret_cast<std::uintptr_t>(p);
    if (totk::engine::isPlausibleAddress(address) &&
        offset + sizeof(out) < totk::engine::kMaximumMappedAddress - address)
        std::memcpy(&out, static_cast<const unsigned char *>(p) + offset, sizeof(out));
    return out;
}
template <class T> void write(void *p, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char *>(p) + offset, &value, sizeof(value));
}
void refuse143(unsigned reason, const char* why) {
    if (!(g_refused.fetch_or(1u << reason) & (1u << reason)))
        Logging.Log("[stasis] SURFACE_REFUSED reason=%u on 1.4.3: %s", reason, why);
}
bool plausible(const void* p) {
    return totk::engine::isPlausibleAddress(reinterpret_cast<std::uintptr_t>(p));
}
void *owner() {
    const auto slot =
        read<void *>(reinterpret_cast<void *>(engine::moduleBase), offsets::kRenderManagerSlot);
    const auto manager = slot ? read<void *>(slot, 0) : nullptr;
    const auto extension = manager ? read<void *>(manager, 0x128) : nullptr;
    return extension ? read<void *>(extension, 0xAC0) : nullptr;
}
bool current(const Presentation &p) {
    return p.aiming || (p.frozen && p.surfaceActor.address && p.surfaceActor.isCurrent(p.scene));
}
void* modelOf(totk::engine::ActorHandle actor) {
    return call<void *(*)(std::uintptr_t)>(offsets::kActorBaseGetModel)(actor.address);
}
bool addModel(totk::engine::ActorHandle actor) {
    auto* model = modelOf(actor);
    const auto count = read<unsigned>(model, 32);
    const auto* units = read<const std::uintptr_t*>(model, 40);
    if (!model || !units || !count || (read<unsigned char>(model, 577) & 1))
        return true; // No drawable model at this LOD.
    if (count > 64 || !totk::engine::isPlausibleAddress(reinterpret_cast<std::uintptr_t>(units))) {
        refuse(6,actor.address,count);
        return false;
    }
    if (g_view.count + count > kUnitCapacity || g_modelCount == engine::kRosterCapacity) {
        refuse(5, actor.address, g_view.count + count);
        return false;
    }
    g_modelActors[g_modelCount] = actor;
    g_models[g_modelCount++] = {model, g_view.count, count};
    for (unsigned i = 0; i < count; ++i)
        if (units[i])
            g_units[g_view.count++] = units[i];
    g_models[g_modelCount-1].count = g_view.count - g_models[g_modelCount-1].first;
    return true;
}
void retainLiveUnits(totk::core::SceneToken scene) {
    unsigned kept = 0;
    for (unsigned i = 0; i < g_modelCount; ++i) {
        const auto& entry = g_models[i];
        const auto actor = g_modelActors[i];
        if (!actor.isCurrent(scene) || modelOf(actor) != entry.model)
            continue;
        const auto count = read<unsigned>(entry.model, 32);
        const auto* units = read<const std::uintptr_t*>(entry.model, 40);
        if (!totk::engine::isPlausibleAddress(reinterpret_cast<std::uintptr_t>(units)) || count > 64)
            continue;
        for (unsigned j = 0; j < entry.count; ++j) {
            const auto unit = g_units[entry.first + j];
            for (unsigned k = 0; k < count; ++k)
                if (units[k] == unit) { g_units[kept++] = unit; break; }
        }
    }
    g_view.count = kept;
}
bool nearby(Bounds b, Vec3 eye) {
    const Vec3 nearest{std::fmax(b.min.x, std::fmin(eye.x,b.max.x)),
                       std::fmax(b.min.y, std::fmin(eye.y,b.max.y)),
                       std::fmax(b.min.z, std::fmin(eye.z,b.max.z))};
    return length(sub(nearest,eye)) <= target::kReach;
}
bool prepareModels(const Presentation& p) {
    g_modelCount = 0;
    g_view.count = 0;
    g_view.units = g_units;
    g_hoverView = {};
    if (p.frozen) return addModel(p.surfaceActor) && g_view.count;
    const auto count = engine::snapshotRoster(p.scene, g_modelActors, engine::kRosterCapacity);
    for (unsigned i = 0; i < count; ++i) {
        const auto actor = g_modelActors[i];
        Bounds bounds;
        if (actor.isCurrent(p.scene) && engine::eligible(actor) &&
            engine::worldBounds(actor.address,bounds) && nearby(bounds,p.previewOrigin) &&
            !addModel(actor)) {
            g_view.count = 0;
            return false;
        }
    }
    for (unsigned i = 0; i < g_modelCount; ++i)
        if (g_modelActors[i].address == p.surfaceActor.address) {
            g_hoverView.units = g_units + g_models[i].first;
            g_hoverView.count = g_models[i].count;
            break;
        }
    return g_view.count != 0;
}
void freeTexture(void* texture) {
    if (!texture) return;
    const auto slot = read<void*>(reinterpret_cast<void*>(engine::moduleBase),
                                 offsets::kDynamicTextureAllocatorSlot);
    if (const auto allocator = read<void*>(slot,0))
        call<void(*)(void*,void*)>(offsets::kDynamicTextureAllocatorFree)(allocator,texture);
    else refuse(7);
}
unsigned selectModelQueue(const ModelView& view,void* queue) {
    unsigned unitCount=0;
    // The native prepare path initialized this type object and uses the same cast.
    const auto type=read<void*>(reinterpret_cast<void*>(engine::moduleBase),offsets::kModelNWTypeSlot);
    if (!type) { refuse(8);return 0; }
    for (unsigned i=0;i<view.count;++i) {
        auto* model=read<void*>(reinterpret_cast<void*>(view.units[i]),0);
        auto* vtable=read<void*>(model,0);
        const auto cast=read<void*(*)(void*,void*)>(vtable,0);
        auto* nw=cast ? cast(model,type) : nullptr;
        if (!nw) continue;
        const auto countOf=read<unsigned(*)(void*)>(vtable,488);
        if (!countOf) { refuse(8);return 0; }
        const auto count=countOf(model);
        const auto* units=read<const std::uintptr_t*>(nw,kDrawUnitList);
        if (!totk::engine::isPlausibleAddress(reinterpret_cast<std::uintptr_t>(units)) ||
            count>kUnitCapacity-unitCount) { refuse(8,0,count);return 0; }
        for (unsigned j=0;j<count;++j) if (units[j]) g_drawEntries[unitCount++]=units[j];
    }
    std::sort(g_drawEntries,g_drawEntries+unitCount);
    const auto count=read<unsigned>(queue,44);
    const auto capacity=read<unsigned>(queue,40);
    const auto* nodes=read<const std::uintptr_t*>(queue,32);
    if (!totk::engine::isPlausibleAddress(reinterpret_cast<std::uintptr_t>(nodes)) ||
        count>capacity || count>kQueueNodeCapacity) { refuse(9,0,count);return 0; }
    const auto selected=selectQueueNodes(nodes,count,g_drawEntries,unitCount,
                                         kUnitCapacity,g_queueMatches,engine::readGame);
    if (selected>kUnitCapacity) { refuse(10,0,selected);return 0; }
    return selected;
}
// Clears the highlight bit on a unit's records and tells the unit (1.4.3 0x9A50EC-0x9A5184).
void clearUnit143(void* unit, unsigned bit) {
    const auto records = read<int (*)(void*)>(read<void*>(unit, 0), 200);
    if (!records) {
        refuse143(14, "a model unit has no record count function, unit skipped");
        return;
    }
    const int count = records(unit);
    for (int j = 0; j < count; ++j) {
        auto* state = read<unsigned char*>(unit, 48);
        auto* record = read<unsigned char*>(state, 8);
        if (!plausible(state) || !plausible(record)) {
            refuse143(13, "a model unit's record list is not a valid address, unit skipped");
            return;
        }
        record += 20 * static_cast<std::ptrdiff_t>(j);
        const auto flags = read<std::uint16_t>(record, 14);
        if (flags & bit) {
            write(record, 14, static_cast<std::uint16_t>(flags & ~bit));
            write(state, 33, static_cast<unsigned char>(read<unsigned char>(state, 33) | 1));
            write(record, 4, read<std::uint32_t>(record, 4) | 2u);
        }
        const auto update = read<void (*)(void*, unsigned)>(read<void*>(unit, 0), 608);
        if (!update) {
            refuse143(17, "a model unit has no update function, unit skipped");
            return;
        }
        update(unit, 1);
    }
}
// The model walk shared by the reset's ring and its four slots (0x9A50B8, 0x9A51C0).
void clearModel143(const void* model, unsigned bit) {
    if (!model || (read<unsigned char>(model, 577) & 1))
        return;
    const int count = read<int>(model, 32);
    for (int u = 0; u < count; ++u) {
        const auto* units = read<const void*>(model, 40);
        if (!plausible(units)) {
            refuse143(12, "a model's unit list is not a valid address, model skipped");
            return;
        }
        const auto* entry = read<const void*>(units, 8 * static_cast<std::size_t>(u));
        if (!plausible(entry)) {
            refuse143(15, "a model's unit entry is not a valid address, unit skipped");
            continue;
        }
        if (auto* unit = read<void*>(entry, 0))
            clearUnit143(unit, bit);
    }
}
// 1.4.3 inlines the renderer reset (1.2.1 0xD59FD4) at 0x9A2E5C; this repeats that copy.
bool resetRenderer143(void* renderer) {
    auto* queue = read<void*>(renderer, 104);
    if (!plausible(renderer) || !plausible(queue)) {
        refuse143(11, "the renderer or its draw queue is not a valid address, highlight skipped");
        return false;
    }
    const auto shift = call<unsigned (*)()>(static_cast<std::uintptr_t>(kHighlightBit143))();
    const unsigned bit = (1u << (shift & 31)) & 0xFFFFu;
    write<std::uint32_t>(renderer, 11212, 0);
    write<std::uint32_t>(queue, 44, 0);
    write<std::uint64_t>(queue, 16, 0xFFFFFFFFu);
    write<std::uint64_t>(queue, 24, 0);
    write<std::uint32_t>(queue, 64, 0);
    const int ring = read<int>(renderer, 56);
    for (int k = 0; k < ring; ++k) {
        const int capacity = read<int>(renderer, 48);
        int index = read<int>(renderer, 52) + k;
        if (index >= capacity)
            index -= capacity;
        const auto* entries = read<const unsigned char*>(renderer, 40);
        if (!plausible(entries) || index < 0) {
            refuse143(16, "the renderer's model ring is not a valid address, ring skipped");
            break;
        }
        clearModel143(read<const void*>(entries, static_cast<std::size_t>(index) * 0x548), bit);
    }
    for (unsigned i = 0; i < 4; ++i)
        clearModel143(read<const void*>(renderer, 64 + 8 * i), bit);
    write<std::uint64_t>(renderer, 52, 0);
    for (unsigned i = 0; i < 4; ++i)
        write<std::uintptr_t>(renderer, 64 + 8 * i, 0);
    return true;
}
bool resetRenderer(void* renderer) {
    if (totk::port::is143())
        return resetRenderer143(renderer);
    call<void (*)(void *)>(offsets::kSpecialPowerRendererReset)(renderer);
    return true;
}
void prepareExtraModels(void* renderer, const void* extras) {
    using Prepare = void (*)(void *, const void *);
    if (totk::port::is143())
        call<Prepare>(static_cast<std::uintptr_t>(kPrepareExtraModels143))(renderer, extras);
    else
        call<Prepare>(offsets::kSpecialPowerRendererPrepareExtraModels)(renderer, extras);
}
void restorePreviousMask(void* renderer, const State& previous, const Presentation* presentation) {
    if (previous.active && reinterpret_cast<std::uintptr_t>(renderer) == previous.owner) {
        // Restore the request before native reset so ordinary abilities keep their mode.
        write(renderer, kNativeRequest, previous.nativeRequest);
        if (!presentation || previous.scene != presentation->scene)
            for (unsigned i = 0; i < 4; ++i)
                write<std::uintptr_t>(renderer, 64 + 8 * i, 0);
        else
            retainLiveUnits(presentation->scene);
        resetRenderer(renderer);
    }
}
void prepareMask(void* renderer, const Presentation& p, State next) {
    if (!renderer) {
        refuse(0, p.surfaceActor.address);
        return;
    }
    if (!prepareModels(p)) return;
    // Reset every native slot; null extra entries do not remove previous models.
    const auto nativeRequest = read<unsigned>(renderer,kNativeRequest);
    if (!resetRenderer(renderer))
        return;
    // The game's extra-model entries are 72 bytes, with the position at 32, 48 and 64.
    alignas(16) unsigned char extras[4 * 72]{};
    write(extras, 0, &g_view);
    write(extras, 32, p.position.x);
    write(extras, 48, p.position.y);
    write(extras, 64, p.position.z);
    prepareExtraModels(renderer, extras);
    if (totk::port::is143() && !g_prepared143.exchange(true))
        Logging.Log("[stasis] 1.4.3 highlight prepared units=%u mode=%u request=%u", g_view.count,
                    read<unsigned>(renderer, 376), nativeRequest);
    next.nativeRequest = nativeRequest;
    write(renderer, kNativeRequest, 1u); // Extra-model preparation sets only the current mode.
    next.owner = reinterpret_cast<std::uintptr_t>(renderer);
    next.actor = p.surfaceActor;
    next.scene = p.scene;
    next.preview = p.aiming;
    next.active = true;
    g_state.write(next);
}
HOOK_DEFINE_TRAMPOLINE(SpecialCalcHook) {
    static void Callback(void *module, void *context) {
        auto *renderer = owner();
        Presentation p;
        const bool published = engine::presentation(p);
        const auto previous = g_state.read();
        restorePreviousMask(renderer, previous, published ? &p : nullptr);
        State next;
        next.epoch = ++g_epoch;
        g_state.write(next);
        Orig(module, context);
        renderer = owner();
        if (!engine::presentation(p) || !current(p))
            return;
        prepareMask(renderer, p, next);
    }
};
template<class Draw> void* drawHoverMask(const State& state, void* renderer, void* queue, Draw draw) {
    void* hoverDepth = nullptr;
    if (state.preview && g_hoverView.count) {
        const auto count=selectModelQueue(g_hoverView,queue);
        if (count) {
            ScopedQueueNodes selected(queue,g_drawEntries,count);
            write(renderer,64,&g_hoverView);
            draw();
            hoverDepth = read<void*>(renderer,kMaskTexture);
            freeTexture(read<void*>(renderer,kSecondMaskTexture));
            write<void*>(renderer,kMaskTexture,nullptr);
            write<void*>(renderer,kSecondMaskTexture,nullptr);
            write(renderer,64,&g_view);
        }
    }
    return hoverDepth;
}
HOOK_DEFINE_TRAMPOLINE(MaskHook) {
    static std::uint64_t Callback(void *renderer, void *context, void *drawContext, unsigned view,
                                  void *buffers) {
        const auto state = g_state.read();
        if (!state.active || view != 0 ||
            reinterpret_cast<std::uintptr_t>(renderer) != state.owner)
            return Orig(renderer,context,drawContext,view,buffers);
        auto* queue=read<void*>(renderer,104);
        if (!totk::engine::isPlausibleAddress(reinterpret_cast<std::uintptr_t>(queue))) {
            markMask(state.epoch,false);
            refuse(9);
            return 0;
        }
        const auto hoverDepth = drawHoverMask(state, renderer, queue, [&] {
            return Orig(renderer,context,drawContext,view,buffers);
        });
        const auto count=selectModelQueue(g_view,queue);
        ScopedQueueNodes all(queue,g_drawEntries,count);
        const auto result = Orig(renderer, context, drawContext, view, buffers);
        const bool valid = count && read<void*>(renderer,kMaskTexture) &&
                           read<void*>(renderer,kSecondMaskTexture);
        markMask(state.epoch,valid);
        g_state.update([&](State& current) {
            if (current.epoch == state.epoch) current.hoverDepth = hoverDepth;
        });
        if (!valid) refuse(2,state.actor.address,count);
        else if (totk::port::is143() && !g_drawn143.exchange(true))
            Logging.Log("[stasis] 1.4.3 highlight mask drawn nodes=%u", count);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(CompositeHook) {
    static std::uint64_t Callback(void *renderer, void *drawContext, void *context, unsigned view,
                                  void *buffers) {
        const auto state = g_state.read();
        if (state.active && view == 0 &&
            reinterpret_cast<std::uintptr_t>(renderer) == state.owner) {
            if constexpr (!render::kNativeBloom)
                render::drawSurface(drawContext, context, buffers);
            freeTexture(state.hoverDepth);
            g_state.update([](State& current) { current.hoverDepth = nullptr; });
            markMask(state.epoch, false);
            return 0; // The caller releases the mask textures straight afterwards.
        }
        return Orig(renderer, drawContext, context, view, buffers);
    }
};
HOOK_DEFINE_TRAMPOLINE(MonochromeHook) {
    static std::uint64_t Callback(void *filter, void *drawContext, unsigned view, void *mask,
                                  int count, void *context, void *buffers) {
        if (g_state.read().active && view == 0)
            return 0;
        return Orig(filter, drawContext, view, mask, count, context, buffers);
    }
};
} // namespace
bool scheduled() { return g_state.read().active; }
bool masks(void *&depth, void *&hoverDepth) {
    const auto state = g_state.read();
    Presentation p;
    auto *renderer = owner();
    if (!state.active || !state.masked || reinterpret_cast<std::uintptr_t>(renderer) != state.owner)
        return false;
    if (!engine::presentation(p) || !current(p) || p.scene != state.scene ||
        p.aiming != state.preview || (!state.preview &&
        (p.surfaceActor.address != state.actor.address ||
         p.surfaceActor.capturedNamePointer != state.actor.capturedNamePointer))) {
        refuse(3, state.actor.address);
        return false;
    }
    depth = read<void *>(renderer, kMaskTexture);
    hoverDepth = state.preview && p.surfaceActor.address == state.actor.address &&
        p.surfaceActor.capturedNamePointer == state.actor.capturedNamePointer ? state.hoverDepth : nullptr;
    if (!depth) {
        refuse(4, state.actor.address);
        return false;
    }
    return true;
}
void install() {
    if (totk::port::is143() &&
        !totk::engine::codeMatches(engine::moduleBase, kHighlight143, "stasis",
                                   "object highlight off on 1.4.3: game code differs at"))
        return;
    SpecialCalcHook::InstallAtOffset(offsets::kSpecialPowerModuleCalc);
    MaskHook::InstallAtOffset(offsets::kSpecialPowerRendererDrawMask);
    CompositeHook::InstallAtOffset(offsets::kSpecialPowerRendererComposite);
    MonochromeHook::InstallAtOffset(offsets::kMonochromeFilterDraw);
    if (totk::port::is143())
        Logging.Log("[stasis] object highlight on 1.4.3: game preparation +%#lx, "
                    "renderer reset copied from the game's inline copy",
                    static_cast<unsigned long>(kPrepareExtraModels143));
}
} // namespace stasis::surface
