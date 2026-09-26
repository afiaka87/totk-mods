// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ArrowboundHookInstaller.hpp"

#include <lib.hpp>

#include "ArrowHookshotService.hpp"
#include "ArrowModeService.hpp"
#include "CarrierSlotAppearance.hpp"
#include "ParasailHandoff.hpp"
#include "HookshotLog.hpp"
#include "WallGripService.hpp"
#include "../../../engine/FlightClock.hpp"
#include "../../../engine/ModelVisibility.hpp"
#include "../../../engine/RagdollTransport.hpp"
#include <arrowbound/ActiveGame.hpp>

namespace {
namespace parasail = arrowbound::parasail;

// 1.0.0-1.2.1: X1 points at the item-name pointer. 1.4.x: X10 holds the name pointer itself.
HOOK_DEFINE_INLINE(PouchSelectionDealHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        ctx->X[1] = arrowbound::arrow_mode::selectionDeal(ctx->X[1]);
    }
};

HOOK_DEFINE_INLINE(PouchSelectionDealNameHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        const auto name = arrowbound::arrow_mode::selectionDeal(
            reinterpret_cast<std::uintptr_t>(&ctx->X[10]));
        ctx->X[10] = *reinterpret_cast<const std::uint64_t*>(name);
    }
};

HOOK_DEFINE_TRAMPOLINE(FindMessageHook) {
    static u64 Callback(void* manager, void* output, const char* const* table, const char* const* key) {
        if (arrowbound::arrow_mode::replaceMessage(output, table, key)) return 0;
        return Orig(manager, output, table, key);
    }
};

HOOK_DEFINE_TRAMPOLINE(NpadControllerCalcImplHook) {
    static void Callback(void* controller) {
        Orig(controller);
        arrowbound::wall_grip::onControllerUpdate(controller);
    }
};

HOOK_DEFINE_TRAMPOLINE(ClimbUpdateHook) {
    static u64 Callback(void* action, void* context) {
        arrowbound::wall_grip::onClimbUpdate(action);
        return Orig(action, context);
    }
};

HOOK_DEFINE_INLINE(ParasailLoopEntryHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        // Integer-only: ANDS W24,W0,#1 follows the relocated ADRP at this site.
        ctx->X[0] = arrowbound::pure::arrowFlightParasailEntry(
            ctx->X[0], arrowbound::arrow_hookshot::keepsParagliderPresented());
    }
};

HOOK_DEFINE_TRAMPOLINE(ParasailEnterHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onEnterHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ParasailUpdateHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onUpdateHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ParasailLeaveHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onLeaveHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(FallEnterHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onFallEnterHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(FallUpdateHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onFallUpdateHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(FallLeaveHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onFallLeaveHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(GlideEntryPredicateHook) {
    static u64 Callback(void* a1, float a2) {
        const bool native = (Orig(a1, a2) & 1) != 0;
        return parasail::onGlideEntryPredicate(native) ? 1 : 0;
    }
};

HOOK_DEFINE_TRAMPOLINE(BowGetResultHook) {
    static u64 Callback(void* action) {
        const u64 native = Orig(action);
        const auto result = arrowbound::pure::arrowFlightBowResult(
            native, arrowbound::arrow_hookshot::keepsParagliderPresented());
        if (result != native) ZHLOG("ARROW_BOW_FINISH native=%llu result=%llu", native, result);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(SerializeSaveDataHook) {
    static u64 Callback(void* manager, u32 fileIndex) {
        const auto result = Orig(manager, fileIndex);
        arrowbound::arrow_mode::filterSerializedSave(manager, fileIndex);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(EquipmentReleaseArrowHook) {
    static u64 Callback(void* equipmentUser) {
        arrowbound::arrow_hookshot::onArrowRelease(equipmentUser);
        return Orig(equipmentUser);
    }
};

HOOK_DEFINE_TRAMPOLINE(ArrowControllerUpdateHook) {
    static u64 Callback(void* controller, float* deltaTime) {
        arrowbound::arrow_hookshot::onArrowUpdate(controller);
        const u64 result = Orig(controller, deltaTime);
#if ARROWBOUND_FLIGHT_DIAGNOSTICS
        arrowbound::arrow_hookshot::onArrowSample(controller, deltaTime ? *deltaTime : 0);
#else
        arrowbound::arrow_hookshot::onArrowSample(controller);
#endif
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ArrowImpactClassifyHook) {
    static u64 Callback(void* controller, int* hitType, void* hitId16,
                        void* motionContext, float* adjustedHit, float deltaTime) {
        const u64 result = Orig(controller, hitType, hitId16, motionContext,
                                adjustedHit, deltaTime);
        arrowbound::arrow_hookshot::onArrowImpact(
            controller, (result & 1u) != 0, hitType ? *hitType : 0,
            adjustedHit, motionContext);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ArrowWorldSweepHook) {
    static u64 Callback(void* controller, bool* a2, bool* a3, unsigned char* a4,
                        unsigned char* a5, unsigned char* a6, bool* a7, float* a8,
                        float step, float unscaledStep, void* motionContext,
                        unsigned char a12) {
        const u64 result = Orig(controller, a2, a3, a4, a5, a6, a7, a8,
                                step, unscaledStep, motionContext, a12);
        arrowbound::arrow_hookshot::onArrowWorldImpact(
            controller, (result & 1u) != 0, a6 && a7 && *a6 && *a7);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(PouchOnSelectSlotHook) {
    static u64 Callback(void* screen, u32 category, u32 index, void* arg4) {
        arrowbound::arrow_mode::beginSelection(category, index);
        const u64 result = Orig(screen, category, index, arg4);
        arrowbound::arrow_mode::endSelection(screen);
        return result;
    }
};

// 1.4.1+: the handler gained a leading result-flag pointer; category and index follow it.
HOOK_DEFINE_TRAMPOLINE(PouchOnSelectSlotFlagHook) {
    static u64 Callback(void* screen, bool* result, u32 category, u32 index, void* arg4) {
        arrowbound::arrow_mode::beginSelection(category, index);
        const u64 value = Orig(screen, result, category, index, arg4);
        arrowbound::arrow_mode::endSelection(screen);
        return value;
    }
};

HOOK_DEFINE_TRAMPOLINE(PouchHandleActionHook) {
    static u64 Callback(void* screen, int action) {
        const bool owned = arrowbound::arrow_mode::beginAction(action);
        const u64 result = Orig(screen, owned ? -1 : action);
        if (owned) arrowbound::arrow_mode::refreshSelectedSlot(screen);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(PouchSetSlotHook) {
    static u64 Callback(void* list, void* slot, void* options) {
        const arrowbound::engine::CarrierSlotAppearance appearance(
            options, arrowbound::arrow_mode::isCarrierSlot(options),
            arrowbound::arrow_mode::enabled());
        return Orig(list, slot, options);
    }
};

bool hookWordMatches(uintptr_t base, const arrowbound::profiles::Site& site,
                     const char* label) {
    const u32 actual = *reinterpret_cast<const u32*>(base + site.offset);
    if (actual == site.word) return true;
    Logging.Log(
        "[arrowbound] HOOK DISABLED %s: main+%p word %08x != expected %08x "
        "(version/cheat conflict)",
        label, reinterpret_cast<void*>(site.offset), actual, site.word);
    return false;
}
}

namespace arrowbound::hooks {
void install(std::uintptr_t mainBase, bool installShared) {
    const auto* game = profiles::active();
    if (!game) return;
    const auto& h = game->hooks;
    game_clock::install(mainBase);
    model_trace::install(mainBase);
    ragdoll_transport::install(mainBase);
    if (installShared) {
#define INSTALL_ENTRY(Hook, Site, Label)                  \
    do {                                              \
        if (profiles::entryHookable(mainBase, Site, Label)) \
            Hook::InstallAtOffset(Site.offset);       \
    } while (false)
        INSTALL_ENTRY(ParasailEnterHook, h.parasailEnter, "parasail enter");
        INSTALL_ENTRY(ParasailUpdateHook, h.parasailUpdate, "parasail update");
        INSTALL_ENTRY(ParasailLeaveHook, h.parasailLeave, "parasail leave");
        INSTALL_ENTRY(FallEnterHook, h.fallEnter, "fall enter");
        INSTALL_ENTRY(FallUpdateHook, h.fallUpdate, "fall update");
        INSTALL_ENTRY(FallLeaveHook, h.fallLeave, "fall leave");
        INSTALL_ENTRY(GlideEntryPredicateHook, h.glideEntry, "glide admission");
#undef INSTALL_ENTRY
        if (hookWordMatches(mainBase, h.gripController, "grip input") &&
            hookWordMatches(mainBase, h.climbUpdate, "grip climb release")) {
            NpadControllerCalcImplHook::InstallAtOffset(h.gripController.offset);
            ClimbUpdateHook::InstallAtOffset(h.climbUpdate.offset);
            arrowbound::runtime().grip.hooksReady.store(1, std::memory_order_release);
        }
    }

#define INSTALL_VERIFIED(Hook, Site, Label)                               \
    do {                                                                    \
        if (hookWordMatches(mainBase, Site, Label))                         \
            Hook::InstallAtOffset(Site.offset);                             \
    } while (false)

    INSTALL_VERIFIED(EquipmentReleaseArrowHook, h.releaseArrow, "arrow release");
    INSTALL_VERIFIED(ArrowControllerUpdateHook, h.arrowUpdate, "arrow update");
    INSTALL_VERIFIED(ArrowImpactClassifyHook, h.arrowImpact, "arrow impact");
    INSTALL_VERIFIED(ArrowWorldSweepHook, h.arrowSweep, "arrow world impact");
    // Install the menu and its action interception together, never a native sage action alone.
    const bool emblemReady =
        hookWordMatches(mainBase, h.pouchSelect, "pouch selection") &&
        hookWordMatches(mainBase, h.pouchSelectArgs, "pouch selection arguments") &&
        hookWordMatches(mainBase, h.pouchAction, "pouch action") &&
        hookWordMatches(mainBase, h.pouchSetSlot, "emblem appearance") &&
        hookWordMatches(mainBase, h.pouchDeal, "emblem menu type") &&
        hookWordMatches(mainBase, h.findMessage, "emblem literal text") &&
        hookWordMatches(mainBase, h.serializeSave, "unsaved emblem carrier");
    if (emblemReady) {
        if (h.pouchSelectArgs.word == arrowbound::profiles::kPouchSelectFlagForm)
            PouchOnSelectSlotFlagHook::InstallAtOffset(h.pouchSelect.offset);
        else
            PouchOnSelectSlotHook::InstallAtOffset(h.pouchSelect.offset);
        PouchHandleActionHook::InstallAtOffset(h.pouchAction.offset);
        PouchSetSlotHook::InstallAtOffset(h.pouchSetSlot.offset);
        if (game->newRenderer())
            PouchSelectionDealNameHook::InstallAtOffset(h.pouchDeal.offset);
        else
            PouchSelectionDealHook::InstallAtOffset(h.pouchDeal.offset);
        FindMessageHook::InstallAtOffset(h.findMessage.offset);
        SerializeSaveDataHook::InstallAtOffset(h.serializeSave.offset);
    }
    arrowbound::arrow_mode::setCodeOnlyReady(emblemReady);
    Logging.Log("[arrowbound] CODE_ONLY_EMBLEM ready=%u", emblemReady);
    INSTALL_VERIFIED(BowGetResultHook, h.bowResult, "released bow completion");
    INSTALL_VERIFIED(ParasailLoopEntryHook, h.parasailLoop, "arrow steady-glide entry");

#undef INSTALL_VERIFIED
}
}
