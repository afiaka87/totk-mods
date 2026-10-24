// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "StasisSurface.hpp"
#include "StasisRender.hpp"
#include "StasisRenderRoute.hpp"
#include "StasisSignals.hpp"
#include "StasisActors.hpp"
#include "StasisModelView.hpp"
#include <lib.hpp>
#include <atomic>
#include <cstring>
namespace stasis::surface {
namespace {
using engine::call;
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
        const auto* units=read<const std::uintptr_t*>(nw,456);
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
void restorePreviousMask(void* renderer, const State& previous, const Presentation* presentation) {
    if (previous.active && reinterpret_cast<std::uintptr_t>(renderer) == previous.owner) {
        // Restore the request before native reset so ordinary abilities keep their mode.
        write(renderer, 372, previous.nativeRequest);
        if (!presentation || previous.scene != presentation->scene)
            for (unsigned i = 0; i < 4; ++i)
                write<std::uintptr_t>(renderer, 64 + 8 * i, 0);
        else
            retainLiveUnits(presentation->scene);
        call<void (*)(void *)>(offsets::kSpecialPowerRendererReset)(renderer);
    }
}
void prepareMask(void* renderer, const Presentation& p, State next) {
    if (!renderer) {
        refuse(0, p.surfaceActor.address);
        return;
    }
    if (!prepareModels(p)) return;
    // Reset every native slot; null extra entries do not remove previous models.
    const auto nativeRequest = read<unsigned>(renderer,372);
    call<void (*)(void *)>(offsets::kSpecialPowerRendererReset)(renderer);
    // The game's extra-model entries are 72 bytes, with the position at 32, 48 and 64.
    alignas(16) unsigned char extras[4 * 72]{};
    write(extras, 0, &g_view);
    write(extras, 32, p.position.x);
    write(extras, 48, p.position.y);
    write(extras, 64, p.position.z);
    call<void (*)(void *, const void *)>(offsets::kSpecialPowerRendererPrepareExtraModels)(
        renderer, extras);
    next.nativeRequest = nativeRequest;
    write(renderer, 372, 1u); // Extra-model preparation sets only the current mode.
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
            hoverDepth = read<void*>(renderer,344);
            freeTexture(read<void*>(renderer,352));
            write<void*>(renderer,344,nullptr);
            write<void*>(renderer,352,nullptr);
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
        const bool valid = count && read<void*>(renderer,344) && read<void*>(renderer,352);
        markMask(state.epoch,valid);
        g_state.update([&](State& current) {
            if (current.epoch == state.epoch) current.hoverDepth = hoverDepth;
        });
        if (!valid) refuse(2,state.actor.address,count);
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
    depth = read<void *>(renderer, 344);
    hoverDepth = state.preview && p.surfaceActor.address == state.actor.address &&
        p.surfaceActor.capturedNamePointer == state.actor.capturedNamePointer ? state.hoverDepth : nullptr;
    if (!depth) {
        refuse(4, state.actor.address);
        return false;
    }
    return true;
}
void install() {
    SpecialCalcHook::InstallAtOffset(offsets::kSpecialPowerModuleCalc);
    MaskHook::InstallAtOffset(offsets::kSpecialPowerRendererDrawMask);
    CompositeHook::InstallAtOffset(offsets::kSpecialPowerRendererComposite);
    MonochromeHook::InstallAtOffset(offsets::kMonochromeFilterDraw);
}
} // namespace stasis::surface
