// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisHooks.hpp"
#include "CryonisEngine.hpp"
#include "CryonisWheel.hpp"
#include "Totk121Layout.hpp"
#include "Totk121Signatures.hpp"
#include <lib.hpp>
#include <cstring>
namespace cryonis::engine {
namespace {
constexpr auto readGame = []<class T>(std::uintptr_t address) { return read<T>(address); };
std::uintptr_t nodeActor(std::uintptr_t node) {
    return call<std::uintptr_t (*)(std::uintptr_t)>(offsets::kExecuteBaseGetActor)(node);
}
template <class Change> void updateWheel(HookContext hook, std::uintptr_t screen, Change change) {
    if (hook.signals.wheelOwner.load() != screen)
        return;
    auto prior = hook.signals.wheel.load();
    auto w = input::unpack(prior);
    if (change(w))
        hook.signals.wheel.compare_exchange_strong(prior, input::pack(w));
}
}
bool validate(std::uintptr_t base) {
    return signatures::codeMatches(base, signatures::kRuntime, "BUILD_REFUSED");
}

void publishPhysicsTime(HookContext hook, float frames) {
    const auto system = read<std::uintptr_t>(moduleBase + offsets::kPhysicsSystem);
    const auto time = valid(system) ? read<std::uintptr_t>(system + layout::physics::kTime) : 0;
    const bool running = valid(time) &&
                         !(read<unsigned>(time + layout::physics::kPausedFlags) & 1) &&
                         std::isfinite(frames) && frames > 0 && frames < 100;
    if (running)
        hook.signals.clock.fetch_add(std::uint64_t(double(frames) * 4294967296.0 + 0.5),
                                     std::memory_order_release);
    hook.signals.running.store(running, std::memory_order_release);
}

void publishCameraRay(HookContext hook, Ray ray) {
    if (hook.signals.cameraLock.test_and_set(std::memory_order_acquire))
        return;
    hook.signals.ray = ray;
    hook.signals.cameraLock.clear(std::memory_order_release);
}

void observeRaycast(HookContext hook, target::Cast cast, const void *from, const void *query) {
    hook.cast.observe(cast, from, query);
}

Ray cameraRay(HookContext hook) {
    if (hook.signals.cameraLock.test_and_set(std::memory_order_acquire))
        return {};
    const Ray ray = hook.signals.ray;
    hook.signals.cameraLock.clear(std::memory_order_release);
    return ray;
}

void wheelOpened(HookContext hook, std::uintptr_t screen) {
    const auto epoch = ++hook.signals.epoch;
    hook.signals.wheelOwner.store(screen);
    hook.signals.wheel.store(input::pack({epoch, true, amiiboSelected(screen, readGame)}));
}

void wheelUpdated(HookContext hook, std::uintptr_t screen) {
    updateWheel(hook, screen, [&](input::Wheel &w) {
        if (!w.open || w.exited)
            return false;
        w.amiibo = amiiboSelected(screen, readGame);
        return true;
    });
}

bool wheelCancelRequested(HookContext hook, std::uintptr_t screen) {
    const auto w = input::unpack(hook.signals.wheel.load());
    return hook.signals.wheelOwner.load() == screen && w.open && w.epoch &&
           hook.signals.request.load() == w.epoch;
}

void wheelExited(HookContext hook, std::uintptr_t screen, bool cancelled) {
    updateWheel(hook, screen, [&](input::Wheel &w) {
        w.open = w.amiibo = false;
        w.exited = true;
        w.cancelled = cancelled;
        return true;
    });
}

void wheelClosed(HookContext hook, std::uintptr_t screen) {
    updateWheel(hook, screen, [](input::Wheel &w) {
        w.open = w.amiibo = false;
        w.closed = true;
        return true;
    });
}

void nativeSelectionAccepted(HookContext hook, std::uintptr_t screen) {
    // Accepted selection is observed before the wrapper clears its decision flag.
    const auto cursor = read<std::int32_t>(screen + layout::wheel::kCursor),
               count = read<std::int32_t>(screen + layout::wheel::kCount);
    if (cursor < 0 || cursor >= count || count < 1 || count > 8)
        return;
    const auto selected = read<std::uint32_t>(screen + layout::wheel::kIds + 4 * cursor);
    hook.signals.nativeSelections.fetch_add(1, std::memory_order_release);
    hook.signals.recentKeyOwned.store(false, std::memory_order_release);
    hook.signals.recentNativeOwned.store(false, std::memory_order_release);
    Logging.Log("[cryonis] NATIVE_SELECTION id=%u cursor=%d", selected, cursor);
}

void maskRecentAbilityKey(HookContext hook, std::uintptr_t node) {
    if (!hook.signals.recentKeyOwned.load(std::memory_order_acquire) ||
        !hook.signals.running.load(std::memory_order_acquire) ||
        nodeActor(node) != hook.signals.keyPlayer.load(std::memory_order_acquire))
        return;
    const auto output = read<std::uintptr_t>(node + layout::node::kOutputs);
    const auto keys = valid(output) ? read<std::uintptr_t>(output) : 0;
    if (!valid(keys))
        return;
    auto &value = *reinterpret_cast<std::uint64_t *>(keys);
    value = input::recentGameplayKeys(value, true);
}

void cancelOwnedAmiibo(HookContext hook, std::uintptr_t node) {
    if (!hook.signals.recentNativeOwned.load(std::memory_order_acquire) || !valid(node))
        return;
    const auto actor = nodeActor(node);
    const auto state = read<unsigned>(node + layout::node::kState);
    if (!input::cancelNativeAmiibo(true, hook.signals.running.load(std::memory_order_acquire),
                                   actor == hook.signals.keyPlayer.load(std::memory_order_acquire),
                                   state))
        return;
    // Native B cancellation owns graph exit, targeting fade and scan/drop teardown.
    auto &flags = *reinterpret_cast<std::uint32_t *>(node + layout::node::kFlags);
    const auto prior = flags;
    flags = input::nativeCancelFlags(prior);
    static unsigned reports{};
    if (reports++ < 12)
        Logging.Log("[cryonis] AMIIBO_AIM_CANCEL state=%u flags=%08x->%08x", state, prior, flags);
}

void frameAimCamera(HookContext hook, std::uintptr_t state, float *output, float frames) {
    const auto player = hook.signals.cameraPlayer.load(), scene = hook.signals.cameraScene.load();
    if (!player || !scene) {
        hook.camera.reset();
        return;
    }
    const auto component = state - layout::camera::kState;
    const auto target =
        valid(component) ? read<std::uintptr_t>(component + layout::camera::kTarget) : 0;
    if (!valid(target) || read<std::uintptr_t>(target + layout::camera::kTargetActor) != player)
        return;
    camera::View view{{output[0], output[1], output[2]},
                      {output[3], output[4], output[5]},
                      {output[6], output[7], output[8]},
                      output[9]};
    if (hook.camera.apply(view, state, scene, hook.signals.aiming.load(),
                          hook.signals.running.load() ? frames : 0))
        std::memcpy(output, &view, sizeof(view));
}
}
