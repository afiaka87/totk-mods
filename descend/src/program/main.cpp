// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <lib.hpp>

#include "engine/DescendOffsets.hpp"
#include "engine/Game.hpp"
#include "feature/Descend.hpp"

#include <cstring>

namespace descend_patch_callbacks {
namespace off = descend::engine::offsets;
namespace feature = descend::feature;

struct NpadCalcHook {
    static void Orig(void* device);
    static void Callback(void* device) {
        Orig(device);
        feature::onNpad(device);
    }
};

struct ClipperUpdateHook {
    static u64 Orig(void* clipper, void* context);
    static u64 Callback(void* clipper, void* context) {
        const auto before = feature::beforeClipperUpdate(clipper);
        const u64 result = Orig(clipper, context);
        feature::afterClipperUpdate(clipper, context, before);
        return result;
    }
};

struct MarkerCastHook {
    DESCEND_INTEGER_ONLY static void Callback(descend::patch::InlineCtx* context) {
        // The saved register block starts with x0, the marker's cast query.
        feature::onMarkerCast(context->X[0], context->X[19]);
    }
};

struct MarkerRingHook {
    static u64 Orig(void* clipper);
    static u64 Callback(void* clipper) {
        return feature::blendMarker(reinterpret_cast<std::uintptr_t>(clipper), 60, 21) ? 0 : Orig(clipper);
    }
};
struct MarkerGridHook {
    static u64 Orig(void* clipper);
    static u64 Callback(void* clipper) {
        return feature::blendMarker(reinterpret_cast<std::uintptr_t>(clipper), 92, 23) ? 0 : Orig(clipper);
    }
};
struct MarkerSoundHook {
    DESCEND_INTEGER_ONLY static void Callback(descend::patch::InlineCtx* context) {
        // X20 is the native effect enum, X3 is ELink-only (0), SLink-only (1), or both (2).
        if ((context->X[20] == 21 || context->X[20] == 23) && feature::warmMarker()) context->X[3] = 0;
    }
};

struct SelectorPositionHook {
    static u64 Orig(void* selector);
    static u64 Callback(void* selector) {
        feature::beforeSelectorPosition(selector);
        return Orig(selector);
    }
};

struct RiseUpdateHook {
    static u64 Orig(void* node);
    static u64 Callback(void* node) {
        if (feature::ownsRiseStage(node)) return 0;
        return Orig(node);
    }
};

struct RiseResultHook {
    static u64 Orig(void* node);
    static u64 Callback(void* node) {
        if (feature::ownsRiseStage(node)) return 2;
        return Orig(node);
    }
};

struct SnapToFloorHook {
    static bool Orig(void* actor, const void* in, void* out);
    static bool Callback(void* actor, const void* in, void* out) {
        if (!feature::holdsTravelTarget(actor, in)) return Orig(actor, in, out);
        std::memmove(out, in, 12);
        return false;
    }
};

struct TravelStateHook {
    static float Orig(void* node);
    static float Callback(void* node) {
        const auto before = feature::beforeTravelState(node);
        const float result = Orig(node);
        feature::afterTravelState(node, before);
        return result;
    }
};

struct CancelCommandHook {
    DESCEND_INTEGER_ONLY static void Callback(descend::patch::InlineCtx* context) {
        // x21 holds the player component, x2 the command the stage is about to play.
        context->X[2] = feature::cancelCommand(context->X[21], context->X[2]);
    }
};

struct TravelAnimationHook {
    static u64 Orig(void* node);
    static u64 Callback(void* node) {
        if (feature::overrideTravelAnimation(node)) return 0;
        return Orig(node);
    }
};

struct CancelUpdateHook {
    static u64 Orig(void* node, void* context);
    static u64 Callback(void* node, void* context) {
        feature::beforeCancelStage(node);
        const u64 result = Orig(node, context);
        feature::afterCancelStage(node);
        return result;
    }
};

struct EffectCalcHook {
    static u64 Orig(void* module, void* context, void* a2, void* a3);
    static u64 Callback(void* module, void* context, void* a2, void* a3) {
        feature::synchronizeScreenCover();
        return Orig(module, context, a2, a3);
    }
};

struct SceneUpdateHook {
    // The patch adapter preserves the complete CPU state before native scene preparation.
    DESCEND_INTEGER_ONLY static void Callback(descend::patch::InlineCtx*) {
        feature::synchronizeScreenCover();
    }
};

struct CancelLeaveHook {
    static u64 Orig(void* node);
    static u64 Callback(void* node) {
        const u64 result = Orig(node);
        feature::afterCancelLeave(node);
        return result;
    }
};

struct EffectWipeChoiceHook {
    DESCEND_INTEGER_ONLY static void Callback(descend::patch::InlineCtx* context) {
        context->X[24] = feature::effectDirection(context->X[24]);
    }
};

struct EffectLoopChoiceHook {
    DESCEND_INTEGER_ONLY static void Callback(descend::patch::InlineCtx* context) {
        context->X[24] = feature::effectDirection(context->X[24]);
    }
};

struct CameraAlphaHook {
    static void Orig(void* camera, float value);
    static void Callback(void* camera, float value) {
        const auto saved = feature::beforeCameraAlpha(camera);
        Orig(camera, value);
        feature::afterCameraAlpha(camera, saved);
    }
};

// The player camera's fade step takes the camera in x0 and a float in s0; its caller ignores the float it returns.
struct PlayerFadeHook {
    static float Orig(void* camera, float value);
    static float Callback(void* camera, float value) {
        const auto saved = feature::beforePlayerFade(camera);
        const float result = Orig(camera, value);
        feature::afterPlayerFade(camera, saved);
        return result;
    }
};

struct PromptSetupHook {
    DESCEND_INTEGER_ONLY static void Callback(descend::patch::InlineCtx* context) {
        // x29 is the routine's frame register; the message and its label sit at fixed places from it.
        const std::uintptr_t frame = context->X[29];
        feature::onPromptMessage(frame + off::prompt::kSetupMessage,
                                 *reinterpret_cast<const std::uintptr_t*>(frame + off::prompt::kSetupLabel), true);
    }
};

struct PromptRefreshHook {
    DESCEND_INTEGER_ONLY static void Callback(descend::patch::InlineCtx* context) {
        const std::uintptr_t frame = context->X[29];
        feature::onPromptMessage(frame + off::prompt::kRefreshMessage,
                                 *reinterpret_cast<const std::uintptr_t*>(frame + off::prompt::kRefreshLabel), false);
    }
};

struct CameraUpdateHook {
    static void Orig(void* camera, float dt, float scale);
    static void Callback(void* camera, float dt, float scale) {
        const auto saved = feature::beforeCameraUpdate(camera);
        Orig(camera, dt, scale);
        feature::afterCameraUpdate(camera, saved);
    }
};
} // namespace
