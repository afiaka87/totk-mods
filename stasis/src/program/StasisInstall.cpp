#include "StasisInstall.hpp"
#include <lib.hpp>
#include "StasisAudio.hpp"
#include "StasisRender.hpp"
#include "StasisRuntime.hpp"
#include "StasisSignals.hpp"
#include "StasisSurface.hpp"
#include "Totk121Offsets.hpp"
#include "totk/port/CodeCheck.hpp"

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
// On 1.4.3 the routine above misses one frame path; the frame-rate update runs every frame (scale at +16).
HOOK_DEFINE_TRAMPOLINE(StasisFrameRateHook) {
    static void Callback(void *rate, float frames) {
        Orig(rate, frames);
        engine::publishPhysicsTime(
            engine::read<float>(reinterpret_cast<std::uintptr_t>(rate) + 16));
    }
};
using namespace totk::port::literals;
constexpr std::ptrdiff_t kFrameRateUpdate143 = 0x3E67F0_v143;
bool frameRateUpdateMatches(std::uintptr_t base) {
    const auto site = base + static_cast<std::uintptr_t>(kFrameRateUpdate143);
    const auto first = totk::suite::checkedCode(site), second = totk::suite::checkedCode(site + 4),
               store = totk::suite::checkedCode(site + 0x30);
    if (totk::engine::entryMatches(first, 0x6DBD23E9) &&
        (second == 0xA9017BFD || totk::engine::chainedAbsoluteTail(site, first)) &&
        store == 0xBD001001)
        return true;
    Logging.Log("[stasis] off on 1.4.3: the frame-rate update reads %08x,%08x store=%08x", first,
                second, store);
    return false;
}
HOOK_DEFINE_TRAMPOLINE(StasisNativeSelectionHook) {
    static std::uintptr_t Callback(std::uintptr_t screen) {
        const auto result = Orig(screen);
        engine::nativeSelectionAccepted(screen);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisAbilityAdmissionHook) {
    static std::uint64_t Callback(std::uintptr_t actor) {
        return engine::nativeAbilityAdmission([](std::uintptr_t player) { return Orig(player); }, actor);
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
// Pause the held object's life simulation alongside its AI/physics. Accepted contacts still
// reach the charge queue. Both phases resume on release; living targets retain normal damage.
HOOK_DEFINE_TRAMPOLINE(StasisLifePreCalcHook) {
    static std::uintptr_t Callback(std::uintptr_t component) {
        return engine::pauseLife(component) ? component : Orig(component);
    }
};
HOOK_DEFINE_TRAMPOLINE(StasisLifeCalcDamageHook) {
    static std::uintptr_t Callback(std::uintptr_t component) {
        return engine::pauseLife(component) ? component : Orig(component);
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
    // Without its clock Stasis could freeze an object and never count the hold down.
    if (totk::port::is143() && !frameRateUpdateMatches(base))
        return false;
    engine::moduleBase = base;
    stasis::render::install();
    stasis::surface::install();
    stasis::audio::install();
    StasisNpadHook::InstallAtOffset(offsets::kNinJoyNpadDeviceCalc);
    if (totk::port::is143())
        StasisFrameRateHook::InstallAtOffset(kFrameRateUpdate143);
    else
        StasisPhysicsClockHook::InstallAtOffset(offsets::kPhysicsModuleUpdateDeltaFrame);
    StasisAcceptedHitHook::InstallAtOffset(offsets::kTgHitInfoSetHitInfo);
    StasisRaycastHook::InstallAtOffset(offsets::kRayCast);
    StasisAiUpdateHook::InstallAtOffset(offsets::kAiElementDoUpdate);
    StasisAnimationUpdateHook::InstallAtOffset(offsets::kAsControllerUpdate);
    StasisEnemyPhysicsHook::InstallAtOffset(offsets::kGamePhysicsPreCalc);
    StasisLifePreCalcHook::InstallAtOffset(offsets::kLifePreCalc);
    StasisLifeCalcDamageHook::InstallAtOffset(offsets::kLifeCalcDamage);
    StasisEnemyPositionHook::InstallAtOffset(offsets::kActorBaseUpdatePositionAndRotation);
    StasisWheelOpenHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenOpenStartImpl);
    StasisWheelUpdateHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenUpdateImpl);
    StasisWheelExitHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenDoOutBefore);
    StasisWheelCloseHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenCloseEndImpl);
    StasisNativeSelectionHook::InstallAtOffset(offsets::kUIShortCutMainItemScreenDoOutBeforeImpl);
    StasisAbilityAdmissionHook::InstallAtOffset(offsets::kNativeAbilityAdmission);
    StasisAimCameraHook::InstallAtOffset(offsets::kUpdateAimCamera);
    Logging.Log("[stasis] v0.2.0 installed");
    return true;
}
bool stasis::chooseFromWheel(std::uintptr_t screen) {
#ifdef TOTK_SUITE_MEMBER
    if (!stasis::runtime::switchedOn()) return false;
#endif
    return engine::wheelChosen(screen);
}
#ifdef TOTK_SUITE_MEMBER
void stasis::switched(bool on) { stasis::runtime::setSwitchedOn(on); }
#endif
