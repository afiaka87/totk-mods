// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
namespace cryonis::offsets {
// TotK 1.2.1 function and global offsets from the module base.
inline constexpr std::uintptr_t kDynamicEquipmentGetUser = 0x666E24;
inline constexpr std::uintptr_t kGetDynamicEquipment = 0x6A7BC0;
inline constexpr std::uintptr_t kRigidBodyBaseGetMotionType = 0x6AB438;
inline constexpr std::uintptr_t kActorBaseForceSetMatrix = 0x6BA86C;
inline constexpr std::uintptr_t kGraphicsContextGraphicsContext = 0x74C19C;
inline constexpr std::uintptr_t kGraphicsContextApply = 0x756A08;
inline constexpr std::uintptr_t kPhysicsModuleUpdateDeltaFrame = 0x7EDC00;
inline constexpr std::uintptr_t kNVNMgrRegisterSampler = 0x7F91E0;
inline constexpr std::uintptr_t kSceneGetGroundHeight = 0x7FE7C0;
inline constexpr std::uintptr_t kBaseProcRequestDelete = 0x83AA68;
inline constexpr std::uintptr_t kActorReferenceGetSystemGroupID = 0x84E674;
inline constexpr std::uintptr_t kRayCast = 0x858590;
inline constexpr std::uintptr_t kShaderProgramActivateSampler = 0x95EF6C;
inline constexpr std::uintptr_t kUtlCreateDynamicUniformBlock = 0x95F604;
inline constexpr std::uintptr_t kModelRenderContextUnbindDefaultFrameBuffer = 0x962C18;
inline constexpr std::uintptr_t kGameActorIsSwimming = 0xA04560;
inline constexpr std::uintptr_t kRigidBodyBaseGetBoundingBoxWorld = 0xA3C1C4;
inline constexpr std::uintptr_t kNVNMgrRegisterTexture = 0xA5B0B4;
inline constexpr std::uintptr_t kUpdateAimCamera = 0xA6DF60;
inline constexpr std::uintptr_t kActorMgrRequestCreateActorAsync = 0xAB92CC;
inline constexpr std::uintptr_t kRigidBodyBaseRequestSetLinearVelocity = 0xACC874;
inline constexpr std::uintptr_t kRigidBodyEntityGetLinearVelocity = 0xACFC80;
inline constexpr std::uintptr_t kHandleFade = 0xAF78BC;
inline constexpr std::uintptr_t kUserResourceSearchAssetCallTableByName = 0xAFD150;
inline constexpr std::uintptr_t kUserInstanceEmitImpl = 0xAFD244;
inline constexpr std::uintptr_t kUIModuleDraw2D = 0xB0F844;
inline constexpr std::uintptr_t kActorBaseGetModel = 0xBADE58;
inline constexpr std::uintptr_t kExecuteBaseGetActor = 0xBC7610;
inline constexpr std::uintptr_t kNativeAbilityAdmission = 0x932610;
inline constexpr std::uintptr_t kModelSceneExtensionDrawPfx = 0xC30C48;
inline constexpr std::uintptr_t kRestoreRenderBuffers = 0xC4B7AC;
inline constexpr std::uintptr_t kModelRenderContextBindDefaultFrameBuffer = 0xC5A6FC;
inline constexpr std::uintptr_t kSceneGetWaterDepth = 0xCE8C94;
inline constexpr std::uintptr_t kGameModelControllerPostInitialize = 0xD98590;
inline constexpr std::uintptr_t kRigidBodyBaseRequestChangeMotionType = 0xE8BE30;
inline constexpr std::uintptr_t kGetModelController = 0x105495C;
inline constexpr std::uintptr_t kGameModelControllerEnableDepthShadowOnly = 0x1082440;
inline constexpr std::uintptr_t kTerrainModuleSearchTerrainSceneByName = 0x1334BAC;
inline constexpr std::uintptr_t kSetModelDepthShadowOnly = 0x15E47B8;
inline constexpr std::uintptr_t kGetDoCommandScreen = 0x19A4AEC;
inline constexpr std::uintptr_t kUIShortCutMainItemScreenDoOutBefore = 0x19AD3F4;
inline constexpr std::uintptr_t kUIShortCutMainItemScreenOpenStartImpl = 0x1B1DC94;
inline constexpr std::uintptr_t kUIShortCutMainItemScreenUpdateImpl = 0x1B1E65C;
inline constexpr std::uintptr_t kUIShortCutMainItemScreenCloseEndImpl = 0x1B1EEB8;
inline constexpr std::uintptr_t kUIShortCutMainItemScreenDoOutBeforeImpl = 0x1B1EEF8;
inline constexpr std::uintptr_t kExecutePlayerAmiiboUpdateImpl = 0x1D29AAC;
inline constexpr std::uintptr_t kExecutePlayerCalcKeyUpdateImpl = 0x1D30CBC;
inline constexpr std::uintptr_t kEventCallbackSLinkReplaceAssetInfo = 0x28402E0;
inline constexpr std::uintptr_t kNinJoyNpadDeviceCalc = 0x2A267BC;
inline constexpr std::uintptr_t kTerrainModuleSlot = 0x462E178;
inline constexpr std::uintptr_t kScreenManagerSlot = 0x462FEC8;
inline constexpr std::uintptr_t kSoundSystem = 0x471A578;
inline constexpr std::uintptr_t kPhysicsSystem = 0x471DCB0;
inline constexpr std::uintptr_t kActorManager = 0x4722920;
inline constexpr std::uintptr_t kGraphicsDevice = 0x4716E48;
inline constexpr std::uintptr_t kNvnGetProcAddress = 0x46707E8;
inline constexpr std::uintptr_t kNvnManagerSlot = 0x462EF90;
inline constexpr std::uintptr_t kUniformAllocator = 0x4717048;
inline constexpr std::uint32_t kDoCommandScreenHash = 0x2D9798A8;
}
