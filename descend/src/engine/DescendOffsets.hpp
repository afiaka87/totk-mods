// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
// TotK 1.2.1 (build 9B4E43650501A4D4) sites and layouts.
#pragma once

#include "totk/engine/CodeCheck.hpp"

#include <cstdint>

namespace descend::engine::offsets {

// Hooked routines.
inline constexpr std::uintptr_t kNpadCalc = 0x2A267BC;
inline constexpr std::uintptr_t kClipperUpdate = 0x175C194;          // CeilingClipper::update
inline constexpr std::uintptr_t kMarkerCast = 0xE50480;              // marker cast call in postCalc
inline constexpr std::uintptr_t kMarkerRing = 0x175D7B8, kMarkerGrid = 0x175D834;
inline constexpr std::uintptr_t kMarkerSound = 0xB580B4;
inline constexpr std::uintptr_t kEmitPowerEffect = 0xB57FE8;
inline constexpr std::uintptr_t kFadeHandles = 0xB94C7C;
inline constexpr std::uintptr_t kFadeEvent = 0xD5B004;
inline constexpr std::uintptr_t kEffectPools = 0x462F290, kEffectSizes = 0x462F298;
inline constexpr std::uintptr_t kSelectorUpdatePosition = 0x215ACC0; // event selector start position
inline constexpr std::uintptr_t kPreUpdate = 0x1C644D0;              // rise-into-ceiling stage update
inline constexpr std::uintptr_t kPreResult = 0x1C6436C;              // rise-into-ceiling stage result
inline constexpr std::uintptr_t kSnapToFloor = 0x1728A30;            // travel-target floor snap
inline constexpr std::uintptr_t kMainUpdateState = 0x1C56A4C;        // travel/preview stage state
inline constexpr std::uintptr_t kMainUpdateAnim = 0x1C56D4C;
inline constexpr std::uintptr_t kCancelUpdate = 0x1C57740;           // cancel (return) stage update
inline constexpr std::uintptr_t kCancelLeave = 0x1C57EC8;
inline constexpr std::uintptr_t kEffectWipeChoice = 0xBB5328, kEffectLoopChoice = 0xBB5360;
inline constexpr std::uintptr_t kCameraUpdate = 0xCB4208;            // GameCamera::updateCamera (render camera)
inline constexpr std::uintptr_t kCancelCommand = 0x1C573EC;          // cancel stage enter, swirl command in x2
inline constexpr std::uintptr_t kEffectCalc = 0xBB3228;              // GameEffectModule::calc
inline constexpr std::uintptr_t kSceneUpdate = 0xCC0670;             // gfx::ModelSceneExtension::update
inline constexpr std::uintptr_t kPromptSetup = 0x1AEEE28;   // UIDoCommandScreen::setupCommandArg, message found
inline constexpr std::uintptr_t kPromptRefresh = 0x1AEF1C8; // UIDoCommandScreen::updateImpl_ per-frame message check
inline constexpr std::uintptr_t kCameraAlpha = 0x15DFE88;   // GameCamera::updateCameraAlpha (camera-distance fade)
inline constexpr std::uintptr_t kPlayerFade = 0xA6EBBC;     // player camera's fade step, run with no event camera

// Called routines and vtable slots.
inline constexpr std::uintptr_t kRaycast = 0x858590;
// VolumeStatsModule::queryCeilingClipperRaycast(module, out, start), Ascend's voxel look upward, and the slot holding the module's holder (double dereference, as CeilingClipper::queryFloorPosition loads it).
inline constexpr std::uintptr_t kVoxelUp = 0xE6E070, kVolumeStatsSlot = 0x462E3E0;
inline constexpr std::uintptr_t kSystemGroupId = 0x161E990;
inline constexpr std::uintptr_t kRayQueryVtableSlot = 0x462E150;
inline constexpr std::uintptr_t kRayFilterVtableSlot = 0x462DF20;
inline constexpr std::uint32_t kLayerCeiling = 0x25;
// The solid-world layer the game's near-player ground probes pass to the same raycast routine (Phantom Foothold, Porter Bridges and Self-Recall cast on it; it also returns dynamic bodies such as rafts).
inline constexpr std::uint32_t kLayerSolid = 0x20;
// Animation command name slots the cancel stage plays: the swirl, then the fall.
inline constexpr std::uintptr_t kCommandSwirl = 0x441DF90, kCommandFall = 0x441DF98;
inline constexpr std::uintptr_t kPlayCommand = 0xBF82B4, kCheckCommand = 0xBF8934;

// Field layouts.
namespace node {
inline constexpr std::uintptr_t kContext = 8, kContextOwner = 8, kOwnerActor = 504;
inline constexpr std::uintptr_t kFlags = 24;
inline constexpr std::uint8_t kAnimationEnabled = 2;
}
namespace actor {
inline constexpr std::uintptr_t kComponents = 552, kPosition = 692, kClothWind = 800;
}
namespace components {
inline constexpr std::uintptr_t kPlayer = 936, kSpecialPower = 1144;
}
namespace power {
inline constexpr std::uintptr_t kCeilingClipper = 56;
}
namespace clipper {
inline constexpr std::uintptr_t kAiming = 8, kPending = 24, kCommitted = 36, kReasons = 124, kFlags = 127;
inline constexpr std::uint8_t kValid = 1, kUndergroundFlags = 0xC;
inline constexpr std::uint16_t kReasonInvalid = 0x40;
}
namespace player {
inline constexpr std::uintptr_t kWaterCancelEffect = 40, kEventFlags = 2788, kCancelByte = 2790,
                                kPreviewByte = 2789, kCancelInputFlags = 2796, kReturnTarget = 4932;
inline constexpr std::uint32_t kCancelRequested = 0x80000, kCancelInput = 0x10;
inline constexpr std::uint32_t kCollisionOff = 0x1000; // in kEventFlags: set by the cancel stage, cleared on restore
inline constexpr std::uint8_t kCancelBit = 8, kPreviewBit = 4;
inline constexpr std::uint64_t kWaterCancelBit = 1ull << 41;
}
namespace selector {
inline constexpr std::uintptr_t kCeilingHeight = 0x20, kCancelLatch = 0x5F;
}
namespace main_stage {
inline constexpr std::uintptr_t kState = 32, kTarget = 60, kPreviewTimer = 116; // kTarget: floor-snapped goal
inline constexpr std::uint32_t kPreview = 5, kLeaving = 6;
inline constexpr float kPreviewTimerRunning = 1.1920929e-7f; // the game's own threshold
}
// kGoal: where the stage carries Link; kSpeed: metres per 30 fps frame; kDone: ends the stage when set.
namespace cancel_stage {
inline constexpr std::uintptr_t kState = 32, kGoal = 60, kClothWind = 72, kSpeed = 84, kSlowdown = 88, kDone = 116;
inline constexpr std::uint32_t kSwirl = 0, kFalling = 3, kArrived = 6;
}
// Doubles the render camera is built from; the override is used when non-zero and cleared after use.
namespace game_camera {
inline constexpr std::uintptr_t kPosition = 232, kLookAt = 272, kPositionOverride = 384, kLookAtOverride = 408;
inline constexpr std::uintptr_t kUp = 296; // floats, refreshed by the game before each update
// Floats the fade routine builds its camera from (position, look-at, up, then the view angle at +196): the game's own camera, not the render override.
inline constexpr std::uintptr_t kFadePosition = 96, kFadeLookAt = 136;
}
// The player camera's own camera floats (position, look-at, up, view angle), written by its update, read by its fade step through CameraAlpha::update and then copied to the game camera.
namespace player_camera {
inline constexpr std::uintptr_t kFadePosition = 0x820, kFadeLookAt = 0x82C;
}
// Ascend's voxel look upward fills: bit 0 of +0 when a unit holds the point, the cell centre at +4/+8, then the first solid going up (+12), where it ends (+16) and the next solid above (+20), each a cell height + 0.5.
namespace voxel_up {
inline constexpr std::uintptr_t kSize = 32, kUnit = 0, kCellX = 4, kFirstSolid = 12, kSolidEnd = 16, kNextSolid = 20;
}
namespace marker {
inline constexpr std::intptr_t kStart = 0x10, kEnd = 0x1C, kMiss = -0x10;
}
// The in-ceiling flag the effect module reads each update to show its swirling screen effect: slot -> global -> owner at +0x128 -> flags at +0xCA4. Set by the camera's ceiling check and the rise stage, cleared on collision restore.
namespace ceiling_effect {
inline constexpr std::uintptr_t kGlobalSlot = 0x462E188, kOwner = 0x128, kFlags = 0xCA4;
inline constexpr std::uint32_t kInCeiling = 0x8000;
}
// The A prompt's message, at both hook sites just after LayoutMsg/DoCommand_00 was searched for the label, from the routine's frame register: setupCommandArg (frame = sp+0x30) and updateImpl_ (frame = sp+0x48). An eui::MessageString is text, length, then the message id the screen caches at +0x1F8 (-1 for a raw string).
namespace prompt {
inline constexpr std::intptr_t kSetupMessage = -0x28, kSetupLabel = 0x18, kRefreshMessage = -0x38,
                               kRefreshLabel = -0x20;
inline constexpr std::intptr_t kText = 0, kLength = 8, kId = 16;
inline constexpr std::uint64_t kRawStringId = ~0ull;
}

// Entry words of every hooked or called routine, then layout words the code above relies on.
inline constexpr totk::engine::CodeSite kSites[]{
    {kNpadCalc, 0xD102C3FF, 0},
    {kClipperUpdate, 0xD0017688, 0xF941F508},
    {kMarkerCast, 0x97F09A16, 0x394213E8},
    {kMarkerRing, 0xD100C3FF, 0xA9017BFD},
    {kMarkerGrid, 0xD100C3FF, 0xA9017BFD},
    {kMarkerSound, 0x940191C3, 0x394043E8},
    {kEmitPowerEffect, 0xD10143FF, 0xA9037BFD},
    {kFadeHandles, 0xA9BE7BFD, 0xA9014FF4},
    {kFadeEvent, 0xA9BB7BFD, 0xF9000BF9},
    {0xE67F90, 0xB941A509, 0xBD00C508}, // event alpha +196, dirty flags +420
    {0xE67F9C, 0xB901A509, 0xAA1303E0},
    {0xE50204, 0xAA0003F3, 0x37005488}, // postCalc owns the live clipper in X19
    {kSelectorUpdatePosition, 0xD107C3FF, 0xFD00D3E8},
    {kPreUpdate, 0xA9BE7BFD, 0xF9000BF3},
    {kPreResult, 0xA9BD7BFD, 0xF9000BF5},
    {kSnapToFloor, 0xD10783FF, 0xA91A7BFD},
    {kMainUpdateState, 0xD10143FF, 0xA9027BFD},
    {kMainUpdateAnim, 0xA9BE7BFD, 0xA9014FF4},
    {kPlayCommand, 0xA9BA7BFD, 0xA9016FFC},
    {kCheckCommand, 0xD104C3FF, 0xA9117BFD},
    {kCancelUpdate, 0xA9BE7BFD, 0xA9014FF4},
    {kCancelLeave, 0xA9BE7BFD, 0xA9014FF4},
    {kEffectWipeChoice, 0x9412C69E, 0x34000098},
    {kEffectLoopChoice, 0x5294F208, 0x8B080276},
    {kCameraUpdate, 0xD10383FF, 0xFD003BE8},
    {kCancelCommand, 0xAA1503E0, 0xAA1F03E1},
    {kEffectCalc, 0x6DB63BEF, 0x6D0133ED},
    {kSceneUpdate, 0xD10383FF, 0xFD003BE8},
    {kRaycast, 0xD10743FF, 0xA9187BFD},
    {kSystemGroupId, 0xA9BD7BFD, 0xF9000BF5},
    {0x137319C, 0xF9411408, 0xB4000068}, // component lookup: components at +552
    {0x175C1E4, 0x3941FC08, 0},          // clipper flags at +127
    {0x175C24C, 0xB9002268, 0},          // pending z at +32
    {0x175C258, 0xF8018E89, 0},          // pending xy at +24
    {0x1C56A68, 0xB9402268, 0},          // travel state at +32
    {0x1C56B10, 0xB9002268, 0},          // confirm sets state 6
    {0x1C56A8C, 0xBC474EA0, 0},          // preview timer at +116
    {0x1C56B18, 0xB9544C08, 0},          // confirm press handled inside the timer branch
    {0x1C572BC, 0xBD5346A0, 0},          // cancel reads return target at player +4932
    {0x1C57758, 0xB9402268, 0},          // cancel stage state at +32
    {0x1C573E4, 0xD0013E22, 0x913E4042}, // x2 = the swirl command slot
    {0x1C573FC, 0x97BE83AE, 0},          // then Player::playCommand
    {0x1C572C0, 0xB94AE6A8, 0x32140108}, // enter sets the collision-off flag
    {0x1C5782C, 0xB94AE688, 0x12137908}, // restore clears it
    {0x1C57724, 0x2D0A8660, 0},          // speed at +84, slowdown distance at +88
    {0x1C57A5C, 0xBD405668, 0},          // falling states move at +84
    {0x1C57D2C, 0x52800028, 0xB9002268}, // swirl done: state 1
    {0x1C57D34, 0xF9419288, 0xB9432A89}, // cloth wind from actor +800
    {0x1C57D3C, 0xB9005269, 0xF9002668}, // to stage +72
    {0x1C571C4, 0x3941D008, 0},          // done byte at +116
    {0x1D53628, 0x5282688A, 0},          // trigger writes return target
    {0x215B240, 0x320D0129, 0},          // cancel request 0x80000
    {0x215B248, 0x39017E7F, 0},          // selector cancel latch +0x5F
    {0xCB42B0, 0x6D580261, 0},           // position override doubles at +384
    {0xCB42D4, 0x6D4F0A60, 0xFD407661},  // final position doubles at +232
    {0xCB4314, 0x6D599263, 0},           // look-at override doubles at +408
    {0xCB4334, 0x6D511263, 0xFD409265},  // final look-at doubles at +272
    {0xCB4344, 0xBD412A66, 0xBD412E67},  // up floats at +296
    {0xBB32DC, 0xF001D3C9, 0xF940C529},  // effect update loads the in-ceiling global through its slot
    {0xBB32E4, 0xF9400129, 0xB40000A9},  // then the global
    {0xBB32EC, 0xF9409529, 0xB4000069},  // then the owner at +0x128
    {0xBB32F4, 0x39729529, 0x3738A4A9},  // and tests bit 0x8000 of +0xCA4
    {0x10ABC00, 0xB94CA509, 0x37780049}, // collision restore clears the same bit
    {kPromptSetup, 0xF940FE69, 0xF9400FE8},   // cached id at screen +0x1F8, then the found id at sp+0x18
    {0x1AEEE14, 0x910023E0, 0x910063A2},      // message at sp+8, label at frame+0x18
    {0x1AEE9C8, 0xD10183FF, 0xA9037BFD},      // setupCommandArg frame
    {0x1AEE9D0, 0x9100C3FD, 0},               // frame = sp+0x30
    {kPromptRefresh, 0xF94013E8, 0xF940FE69}, // found id at sp+0x20, then the cached id at screen +0x1F8
    {0x1AEF1B4, 0x910043E0, 0xD10083A2},      // message at sp+0x10, label at frame-0x20
    {0x1AEF014, 0xA904FBFD, 0x910123FD},      // updateImpl_ frame = sp+0x48
    {0x1235FE4, 0x92800008, 0xF9000808},      // setMessage: a raw string's id is -1, at +16
    {kCameraAlpha, 0xD10203FF, 0xFD0033E8},
    {0x15DFEB0, 0x91018001, 0x91022002}, // fade camera position at +96, look-at at +136
    {kPlayerFade, 0xD10283FF, 0x6D0323E9},
    {0xA6EC14, 0xF9462668, 0x9120826A}, // player fade hands CameraAlpha::update the camera floats at +0x820
    {kVoxelUp, 0xA9BC7BFD, 0xF9000BF7},
    {0x175D0D8, 0xF941F108, 0xF9400117}, // queryFloorPosition: module = *(*(slot 0x462E3E0))
};

} // namespace descend::engine::offsets
