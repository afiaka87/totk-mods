#include "StasisInstall.hpp"
#include <lib.hpp>
#include "StasisAudio.hpp"
#include "StasisRender.hpp"
#include "StasisRuntime.hpp"
#include "StasisSignals.hpp"
#include "StasisSurface.hpp"
#include "Totk121Offsets.hpp"

namespace {
namespace engine = stasis::engine;
HOOK_DEFINE_TRAMPOLINE(StasisAimCameraHook) {
    static void Callback(std::uintptr_t state, float *view, float frameScale) {
        Orig(state, view, frameScale);
        engine::frameAimCamera(state, view, frameScale);
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisWheelOpenHook) {
    static float Callback(std::uintptr_t screen) {
        const auto result = Orig(screen);
        engine::wheelOpened(screen);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisWheelUpdateHook) {
    static void Callback(std::uintptr_t screen) {
        Orig(screen);
        engine::wheelUpdated(screen);
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisWheelExitHook) {
    static std::uintptr_t Callback(std::uintptr_t screen, unsigned char cancel) {
        if (engine::wheelCancelRequested(screen))
            cancel = 1;
        const auto result = Orig(screen, cancel);
        engine::wheelExited(screen, (cancel & 1) != 0);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisWheelCloseHook) {
    static std::uintptr_t Callback(std::uintptr_t screen) {
        const auto result = Orig(screen);
        engine::wheelClosed(screen);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisNpadHook) {
    static void Callback(void *device) {
        Orig(device);
        stasis::runtime::tick(device);
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisPhysicsClockHook) {
    static void Callback(void *module, const void *pauseContext, float frameScale) {
        Orig(module, pauseContext, frameScale);
        engine::publishPhysicsTime(frameScale);
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisAcceptedHitHook) {
    static std::uint64_t Callback(void *record, void *contact, void *victimComponent,
                                  void *attackInfo) {
        const auto result = Orig(record, contact, victimComponent, attackInfo);
        engine::acceptedHit(record, victimComponent, attackInfo);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisRaycastHook) {
    static std::uint64_t Thunk(const void *from, const void *to, const void *object,
                               const void *out, std::uint32_t mask, std::uint32_t flags) {
        return Orig(from, to, object, out, mask, flags);
    }
    static std::uint64_t Callback(const void *from, const void *to, const void *object,
                                  const void *out, std::uint32_t mask, std::uint32_t flags) {
        const auto result = Orig(from, to, object, out, mask, flags);
        engine::observeRaycast(Thunk, from, object);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisAiUpdateHook) {
    static std::uintptr_t Callback(std::uintptr_t node, void *context) {
        if (engine::pauseAiNode(node))
            return node;
        return Orig(node, context);
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisAnimationUpdateHook) {
    static std::uintptr_t Callback(std::uintptr_t controller, unsigned char force, float rate,
                                   std::uintptr_t groups, unsigned index, unsigned char mode,
                                   void *output) {
        if (engine::pauseAnimation(controller))
            return controller;
        return Orig(controller, force, rate, groups, index, mode, output);
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisEnemyPhysicsHook) {
    static std::uintptr_t Callback(void *component, const void *step, unsigned char mode) {
        const auto result = Orig(component, step, mode);
        engine::holdEnemyPhysics(component);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisEnemyPositionHook) {
    static std::uintptr_t Thunk(std::uintptr_t actor, float *matrix, float frameScale) {
        return Orig(actor, matrix, frameScale);
    }
    static std::uintptr_t Callback(std::uintptr_t actor, float *matrix, float frameScale) {
        return engine::updateEnemyPosition(Thunk, actor, matrix, frameScale);
    }
};
} // namespace

bool stasis::install(std::uintptr_t base) {
    if (!engine::validate(base))
        return false;
    engine::moduleBase = base;
    stasis::render::install();
    stasis::surface::install();
    stasis::audio::install();
    StasisNpadHook::InstallAtOffset(offsets::kNinJoyNpadDeviceCalc);
    StasisPhysicsClockHook::InstallAtOffset(offsets::kPhysicsModuleUpdateDeltaFrame);
    StasisAcceptedHitHook::InstallAtOffset(offsets::kTgHitInfoSetHitInfo);
    StasisRaycastHook::InstallAtOffset(offsets::kRayCast);
    StasisAiUpdateHook::InstallAtOffset(offsets::kAiElementDoUpdate);
    StasisAnimationUpdateHook::InstallAtOffset(offsets::kAsControllerUpdate);
    StasisEnemyPhysicsHook::InstallAtOffset(offsets::kGamePhysicsPreCalc);
    StasisEnemyPositionHook::InstallAtOffset(offsets::kActorBaseUpdatePositionAndRotation);
    StasisWheelOpenHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenOpenStartImpl);
    StasisWheelUpdateHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenUpdateImpl);
    StasisWheelExitHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenDoOutBefore);
    StasisWheelCloseHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenCloseEndImpl);
    StasisAimCameraHook::InstallAtOffset(offsets::kUpdateAimCamera);
    Logging.Log("[stasis] v0.1.17 installed");
    return true;
}
