// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "StasisSurface.hpp"
#include "StasisRender.hpp"
#include "StasisRenderRoute.hpp"
#include "StasisSignals.hpp"
#include <lib.hpp>
#include <atomic>
#include <cstring>
namespace stasis::surface {
namespace {
using engine::call;
struct State {
    std::uintptr_t owner{};
    totk::engine::ActorHandle actor{};
    unsigned epoch{}, nativeRequest{};
    bool active{}, masked{};
};
Shared<State> g_state;
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
    return (p.frozen || (p.aiming && p.selected)) && p.surfaceActor.address &&
           p.surfaceActor.isCurrent(p.scene);
}
HOOK_DEFINE_TRAMPOLINE(SpecialCalcHook) {
    static void Callback(void *module, void *context) {
        auto *renderer = owner();
        Presentation p;
        const bool published = engine::presentation(p);
        const auto previous = g_state.read();
        // The game's frame sequence owns model and material changes; no lock spans its calls.
        if (previous.active && reinterpret_cast<std::uintptr_t>(renderer) == previous.owner) {
            // The game copies the requested mode into the current one each frame; restoring the request first lets the game's own abilities keep it.
            write(renderer, 372, previous.nativeRequest);
            if (!published || !previous.actor.isCurrent(p.scene))
                for (unsigned i = 0; i < 4; ++i)
                    write<std::uintptr_t>(renderer, 64 + 8 * i, 0);
            call<void (*)(void *)>(offsets::kSpecialPowerRendererReset)(renderer);
        }
        State next;
        next.epoch = ++g_epoch;
        g_state.write(next);
        Orig(module, context);
        renderer = owner();
        if (!engine::presentation(p) || !current(p))
            return;
        if (!renderer) {
            refuse(0, p.surfaceActor.address);
            return;
        }
        auto *model =
            call<void *(*)(std::uintptr_t)>(offsets::kActorBaseGetModel)(p.surfaceActor.address);
        const unsigned count = read<unsigned>(model, 32);
        if (!model || count < 1 || count > 64 || (read<unsigned char>(model, 577) & 1)) {
            refuse(1, p.surfaceActor.address, count);
            return;
        }
        // The game's extra-model entries are 72 bytes, with the position at 32, 48 and 64.
        alignas(16) unsigned char extras[4 * 72]{};
        write(extras, 0, model);
        write(extras, 32, p.position.x);
        write(extras, 48, p.position.y);
        write(extras, 64, p.position.z);
        call<void (*)(void *, const void *)>(offsets::kSpecialPowerRendererPrepareExtraModels)(
            renderer, extras);
        next.nativeRequest = read<unsigned>(renderer, 372);
        write(renderer, 372, 1u); // Extra-model preparation sets only the current mode.
        next.owner = reinterpret_cast<std::uintptr_t>(renderer);
        next.actor = p.surfaceActor;
        next.active = true;
        g_state.write(next);
    }
};
HOOK_DEFINE_TRAMPOLINE(MaskHook) {
    static std::uint64_t Callback(void *renderer, void *context, void *drawContext, unsigned view,
                                  void *buffers) {
        const auto result = Orig(renderer, context, drawContext, view, buffers);
        const auto state = g_state.read();
        if (state.active && view == 0 &&
            reinterpret_cast<std::uintptr_t>(renderer) == state.owner) {
            const auto *queue = read<void *>(renderer, 104);
            const unsigned count = read<unsigned>(queue, 44);
            const bool valid =
                count > 0 && read<void *>(renderer, 344) && read<void *>(renderer, 352);
            markMask(state.epoch, valid);
            if (!valid)
                refuse(2, state.actor.address, count);
        }
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
bool masks(void *&depth, void *&identity) {
    const auto state = g_state.read();
    Presentation p;
    auto *renderer = owner();
    if (!state.active || !state.masked || reinterpret_cast<std::uintptr_t>(renderer) != state.owner)
        return false;
    if (!engine::presentation(p) || !current(p) || p.surfaceActor.address != state.actor.address ||
        p.surfaceActor.capturedNamePointer != state.actor.capturedNamePointer ||
        p.surfaceActor.scene != state.actor.scene) {
        refuse(3, state.actor.address);
        return false;
    }
    depth = read<void *>(renderer, 344);
    identity = read<void *>(renderer, 352);
    if (!depth || !identity) {
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
