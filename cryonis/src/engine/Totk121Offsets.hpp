// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>
#if defined(TOTK_PORT)
#include "totk/port/Port.hpp"
#endif
namespace cryonis::offsets {
// TotK 1.2.1 function and global offsets from the module base.
#if defined(TOTK_PORT)
// Module builds: each offset names its 1.2.1 place and gives the running game's place where used.
using Offset = totk::port::Code;
#else
using Offset = std::uintptr_t;
#endif
inline constexpr Offset kDynamicEquipmentGetUser = 0x666E24;
inline constexpr Offset kGetDynamicEquipment = 0x6A7BC0;
inline constexpr Offset kRigidBodyBaseGetMotionType = 0x6AB438;
inline constexpr Offset kActorBaseForceSetMatrix = 0x6BA86C;
inline constexpr Offset kGraphicsContextGraphicsContext = 0x74C19C;
inline constexpr Offset kGraphicsContextApply = 0x756A08;
inline constexpr Offset kPhysicsModuleUpdateDeltaFrame = 0x7EDC00;
inline constexpr Offset kNVNMgrRegisterSampler = 0x7F91E0;
inline constexpr Offset kSceneGetGroundHeight = 0x7FE7C0;
inline constexpr Offset kBaseProcRequestDelete = 0x83AA68;
inline constexpr Offset kActorReferenceGetSystemGroupID = 0x84E674;
inline constexpr Offset kRayCast = 0x858590;
inline constexpr Offset kShaderProgramActivateSampler = 0x95EF6C;
inline constexpr Offset kUtlCreateDynamicUniformBlock = 0x95F604;
inline constexpr Offset kModelRenderContextUnbindDefaultFrameBuffer = 0x962C18;
inline constexpr Offset kGameActorIsSwimming = 0xA04560;
inline constexpr Offset kRigidBodyBaseGetBoundingBoxWorld = 0xA3C1C4;
inline constexpr Offset kNVNMgrRegisterTexture = 0xA5B0B4;
inline constexpr Offset kUpdateAimCamera = 0xA6DF60;
inline constexpr Offset kActorMgrRequestCreateActorAsync = 0xAB92CC;
inline constexpr Offset kRigidBodyBaseRequestSetLinearVelocity = 0xACC874;
inline constexpr Offset kRigidBodyEntityGetLinearVelocity = 0xACFC80;
inline constexpr Offset kHandleFade = 0xAF78BC;
inline constexpr Offset kUserResourceSearchAssetCallTableByName = 0xAFD150;
inline constexpr Offset kUserInstanceEmitImpl = 0xAFD244;
inline constexpr Offset kUIModuleDraw2D = 0xB0F844;
inline constexpr Offset kActorBaseGetModel = 0xBADE58;
inline constexpr Offset kExecuteBaseGetActor = 0xBC7610;
inline constexpr Offset kNativeAbilityAdmission = 0x932610;
inline constexpr Offset kModelSceneExtensionDrawPfx = 0xC30C48;
inline constexpr Offset kRestoreRenderBuffers = 0xC4B7AC;
inline constexpr Offset kModelRenderContextBindDefaultFrameBuffer = 0xC5A6FC;
inline constexpr Offset kSceneGetWaterDepth = 0xCE8C94;
inline constexpr Offset kGameModelControllerPostInitialize = 0xD98590;
inline constexpr Offset kRigidBodyBaseRequestChangeMotionType = 0xE8BE30;
inline constexpr Offset kGetModelController = 0x105495C;
inline constexpr Offset kGameModelControllerEnableDepthShadowOnly = 0x1082440;
inline constexpr Offset kTerrainModuleSearchTerrainSceneByName = 0x1334BAC;
inline constexpr Offset kSetModelDepthShadowOnly = 0x15E47B8;
inline constexpr Offset kGetDoCommandScreen = 0x19A4AEC;
inline constexpr Offset kUIShortCutMainItemScreenDoOutBefore = 0x19AD3F4;
inline constexpr Offset kUIShortCutMainItemScreenOpenStartImpl = 0x1B1DC94;
inline constexpr Offset kUIShortCutMainItemScreenUpdateImpl = 0x1B1E65C;
inline constexpr Offset kUIShortCutMainItemScreenCloseEndImpl = 0x1B1EEB8;
inline constexpr Offset kUIShortCutMainItemScreenDoOutBeforeImpl = 0x1B1EEF8;
inline constexpr Offset kExecutePlayerAmiiboUpdateImpl = 0x1D29AAC;
inline constexpr Offset kExecutePlayerCalcKeyUpdateImpl = 0x1D30CBC;
inline constexpr Offset kEventCallbackSLinkReplaceAssetInfo = 0x28402E0;
inline constexpr Offset kNinJoyNpadDeviceCalc = 0x2A267BC;
inline constexpr Offset kTerrainModuleSlot = 0x462E178;
inline constexpr Offset kScreenManagerSlot = 0x462FEC8;
inline constexpr Offset kSoundSystem = 0x471A578;
inline constexpr Offset kPhysicsSystem = 0x471DCB0;
inline constexpr Offset kActorManager = 0x4722920;
inline constexpr Offset kGraphicsDevice = 0x4716E48;
inline constexpr Offset kNvnGetProcAddress = 0x46707E8;
inline constexpr Offset kNvnManagerSlot = 0x462EF90;
inline constexpr Offset kUniformAllocator = 0x4717048;
inline constexpr std::uint32_t kDoCommandScreenHash = 0x2D9798A8;
}
