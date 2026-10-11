// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "Totk121Offsets.hpp"
#include "totk/engine/CodeCheck.hpp"
namespace cryonis::signatures {
using Site = totk::engine::CodeSite;
inline constexpr Site kRuntime[]{
    {offsets::kPhysicsModuleUpdateDeltaFrame, 0xF9400828, 0xB40002A8},
    {offsets::kNinJoyNpadDeviceCalc, 0xD102C3FF, 0xA9057BFD},
    {offsets::kRayCast, 0xD10743FF, 0xA9187BFD},
    {offsets::kActorBaseForceSetMatrix, 0xA9BD7BFD, 0xF9000BF5},
    {offsets::kRigidBodyBaseRequestSetLinearVelocity, 0xFC1B0FEA, 0x6D0123E9},
    {offsets::kRigidBodyBaseRequestChangeMotionType, 0xA9BD7BFD, 0xF9000BF5},
    {offsets::kActorMgrRequestCreateActorAsync, 0xA9BA7BFD, 0xA9016FFC},
    {offsets::kSceneGetWaterDepth, 0xD102C3FF, 0xFD0033EA},
    {offsets::kSceneGetGroundHeight, 0xD10243FF, 0xA9047BFD},
    {offsets::kTerrainModuleSearchTerrainSceneByName, 0xA9BD7BFD, 0xA90157F6},
    {offsets::kBaseProcRequestDelete, 0xD10143FF, 0xA9017BFD},
    {offsets::kActorReferenceGetSystemGroupID, 0xF9400009, 0xB4000189},
    {offsets::kRigidBodyBaseGetBoundingBoxWorld, 0xD10243FF, 0xA9067BFD},
    {offsets::kUIShortCutMainItemScreenOpenStartImpl, 0xD10283FF, 0xFD001BE8},
    {offsets::kUIShortCutMainItemScreenUpdateImpl, 0xD10203FF, 0x6D032BEB},
    {offsets::kUIShortCutMainItemScreenDoOutBefore, 0xA9BE7BFD, 0xA9014FF4},
    {offsets::kUIShortCutMainItemScreenDoOutBeforeImpl, 0xD10103FF, 0xA9017BFD},
    {offsets::kUIShortCutMainItemScreenCloseEndImpl, 0xA9BE7BFD, 0xA9014FF4},
    {offsets::kUpdateAimCamera, 0xD102C3FF, 0x6D033BEF},
    {offsets::kGetModelController, 0xF9411408, 0xB4000068},
    {offsets::kGameModelControllerEnableDepthShadowOnly, 0x39427409, 0x12000028},
    {offsets::kSetModelDepthShadowOnly, 0xA9BD7BFD, 0xA90157F6},
    {offsets::kActorBaseGetModel, 0xF9411408, 0xB40000C8},
    {offsets::kGetDynamicEquipment, 0xF9411408, 0xB4000068},
    {offsets::kDynamicEquipmentGetUser, 0xA9BC7BFD, 0xF9000BF7},
    {offsets::kRigidBodyBaseGetMotionType, 0xF9403008, 0xB4000068},
    {offsets::kRigidBodyEntityGetLinearVelocity, 0xF9403408, 0x373800A8},
    {offsets::kExecutePlayerCalcKeyUpdateImpl, 0xD10143FF, 0xA9027BFD},
    {offsets::kExecuteBaseGetActor, 0xF9400408, 0xF9400508},
    {offsets::kExecutePlayerAmiiboUpdateImpl, 0xA9BE7BFD, 0xF9000BF3},
    {offsets::kGameActorIsSwimming, 0xA9BF7BFD, 0x910003FD},
    {offsets::kGameModelControllerPostInitialize, 0xD10503FF, 0xFD006BE8}};
inline constexpr Site kRender[]{
    {offsets::kNVNMgrRegisterSampler, 0xD10283FF, 0xA9047BFD},
    {offsets::kNVNMgrRegisterTexture, 0xD101C3FF, 0xA9017BFD},
    {offsets::kUtlCreateDynamicUniformBlock, 0xD103C3FF, 0xA9097BFD},
    {offsets::kShaderProgramActivateSampler, 0xD10143FF, 0xA9017BFD},
    {offsets::kGraphicsContextGraphicsContext, 0x52802028, 0xD280A0A9},
    {offsets::kModelRenderContextBindDefaultFrameBuffer, 0xA9BE7BFD, 0xA9014FF4},
    {offsets::kGraphicsContextApply, 0xD10243FF, 0xA9037BFD},
    {offsets::kModelRenderContextUnbindDefaultFrameBuffer, 0xF942A000, 0xB4000080},
    {offsets::kRestoreRenderBuffers, 0xA9BE7BFD, 0xA9014FF4}};
inline constexpr Site kUi[]{{offsets::kUIModuleDraw2D, 0xD10283FF, 0xA9067BFD}};
inline constexpr Site kPrompt[]{{offsets::kGetDoCommandScreen, 0xA9BE7BFD, 0xF9000BF3}};
inline constexpr Site kPfx{offsets::kModelSceneExtensionDrawPfx, 0xD104C3FF, 0xA90D7BFD};
inline constexpr Site kAudio[]{
    {offsets::kEventCallbackSLinkReplaceAssetInfo, 0xA9BD7BFD, 0xA90157F6},
    {offsets::kUserResourceSearchAssetCallTableByName, 0xA9BA7BFD, 0},
    {offsets::kUserInstanceEmitImpl, 0xA9BA7BFD, 0},
    {offsets::kHandleFade, 0xA9BB7BFD, 0xF9000BF9}};
}
