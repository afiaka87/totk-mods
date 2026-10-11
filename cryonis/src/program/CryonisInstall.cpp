// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisInstall.hpp"
#include <lib.hpp>
#include "Totk121Offsets.hpp"
#include "CryonisRuntime.hpp"
#include "CryonisHooks.hpp"
#include "CryonisWorld.hpp"
#include "CryonisRender.hpp"
#include "CryonisHud.hpp"
#include "CryonisAudio.hpp"
namespace {
namespace cn = cryonis::runtime;
namespace engine = cryonis::engine;
namespace offsets = cryonis::offsets;
HOOK_DEFINE_TRAMPOLINE(CryonisNpadHook) {
    static void Callback(void *device) {
        Orig(device);
        cn::tick(device);
    }
};
HOOK_DEFINE_TRAMPOLINE(CryonisPlayerKeyHook) {
    static std::uintptr_t Callback(std::uintptr_t node) {
        const auto result = Orig(node);
        engine::maskRecentAbilityKey(cn::hooks(), node);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(CryonisAmiiboHook) {
    static std::uintptr_t Callback(std::uintptr_t node) {
        const auto result = Orig(node);
        engine::cancelOwnedAmiibo(cn::hooks(), node);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(CryonisModelInitHook) {
    static std::uintptr_t Callback(std::uintptr_t component, void *context) {
        const auto result = Orig(component, context);
        engine::modelInitialized(component);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(CryonisClockHook) {
    static void Callback(void *module, const void *pause, float frames) {
        Orig(module, pause, frames);
        engine::publishPhysicsTime(cn::hooks(), frames);
    }
};
HOOK_DEFINE_TRAMPOLINE(CryonisCameraHook) {
    static void Callback(std::uintptr_t state, float *view, float frames) {
        Orig(state, view, frames);
        engine::frameAimCamera(cn::hooks(), state, view, frames);
    }
};
HOOK_DEFINE_TRAMPOLINE(CryonisWheelOpenHook) {
    static float Callback(std::uintptr_t screen) {
        const auto result = Orig(screen);
        engine::wheelOpened(cn::hooks(), screen);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(CryonisWheelUpdateHook) {
    static void Callback(std::uintptr_t screen) {
        Orig(screen);
        engine::wheelUpdated(cn::hooks(), screen);
    }
};
HOOK_DEFINE_TRAMPOLINE(CryonisWheelExitHook) {
    static std::uintptr_t Callback(std::uintptr_t screen, unsigned char cancel) {
        if (engine::wheelCancelRequested(cn::hooks(), screen))
            cancel = 1;
        const auto result = Orig(screen, cancel);
        engine::wheelExited(cn::hooks(), screen, (cancel & 1) != 0);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(CryonisNativeSelectionHook) {
    static std::uintptr_t Callback(std::uintptr_t screen) {
        const auto result = Orig(screen);
        engine::nativeSelectionAccepted(cn::hooks(), screen);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(CryonisWheelCloseHook) {
    static std::uintptr_t Callback(std::uintptr_t screen) {
        const auto result = Orig(screen);
        engine::wheelClosed(cn::hooks(), screen);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(CryonisRayHook) {
    static std::uint64_t Thunk(const void *from, const void *to, const void *query, const void *out,
                               unsigned mask, unsigned flags) {
        return Orig(from, to, query, out, mask, flags);
    }
    static std::uint64_t Callback(const void *from, const void *to, const void *query,
                                  const void *out, unsigned mask, unsigned flags) {
        const auto result = Orig(from, to, query, out, mask, flags);
        engine::observeRaycast(cn::hooks(), Thunk, from, query);
        return result;
    }
};
}
bool cryonis::install(std::uintptr_t base) {
    if (!engine::validate(base))
        return false;
    cn::initialize(base);
    cryonis::render::install(base);
    cryonis::hud::install(base);
    cryonis::audio::install(base);
    CryonisNpadHook::InstallAtOffset(offsets::kNinJoyNpadDeviceCalc);
    CryonisClockHook::InstallAtOffset(offsets::kPhysicsModuleUpdateDeltaFrame);
    CryonisCameraHook::InstallAtOffset(offsets::kUpdateAimCamera);
    CryonisRayHook::InstallAtOffset(offsets::kRayCast);
    CryonisWheelOpenHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenOpenStartImpl);
    CryonisWheelUpdateHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenUpdateImpl);
    CryonisWheelExitHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenDoOutBefore);
    CryonisWheelCloseHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenCloseEndImpl);
    CryonisNativeSelectionHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenDoOutBeforeImpl);
    CryonisPlayerKeyHook::InstallAtOffset(offsets::kExecutePlayerCalcKeyUpdateImpl);
    CryonisAmiiboHook::InstallAtOffset(offsets::kExecutePlayerAmiiboUpdateImpl);
    CryonisModelInitHook::InstallAtOffset(offsets::kGameModelControllerPostInitialize);
    Logging.Log("[cryonis] water targeting and original-reference mesh renderer installed");
    return true;
}
