// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
namespace stasis::offsets {
// TotK 1.2.1 function offsets from the module base.
inline constexpr std::uintptr_t kGamePhysicsPreCalc = 0x6359F4;
inline constexpr std::uintptr_t kAsControllerUpdate = 0x6437BC;
inline constexpr std::uintptr_t kDynamicEquipmentGetUser = 0x666E24;
inline constexpr std::uintptr_t kGetDynamicEquipment = 0x6A7BC0;
inline constexpr std::uintptr_t kRigidBodyBaseGetMotionType = 0x6AB438;
inline constexpr std::uintptr_t kActorBaseForceSetMatrix = 0x6BA86C;
inline constexpr std::uintptr_t kActorLinkBaseGetReference = 0x6CB510;
inline constexpr std::uintptr_t kGraphicsContextGraphicsContext = 0x74C19C;
inline constexpr std::uintptr_t kAiElementDoUpdate = 0x74FF2C;
inline constexpr std::uintptr_t kGraphicsContextApply = 0x756A08;
inline constexpr std::uintptr_t kPhysicsModuleUpdateDeltaFrame = 0x7EDC00;
inline constexpr std::uintptr_t kActorBaseUpdatePositionAndRotation = 0x823FD8;
inline constexpr std::uintptr_t kActorReferenceGetSystemGroupID = 0x84E674;
inline constexpr std::uintptr_t kRayCast = 0x858590;
inline constexpr std::uintptr_t kRigidBodyGetCenterOfMass = 0x85C240;
inline constexpr std::uintptr_t kActorSetPose = 0x860020;
inline constexpr std::uintptr_t kTgHitInfoSetHitInfo = 0x8638B8;
inline constexpr std::uintptr_t kControllerSetVisitRigidBodies = 0x8AA374;
inline constexpr std::uintptr_t kShaderProgramActivateSampler = 0x95EF6C;
inline constexpr std::uintptr_t kUtlCreateDynamicUniformBlock = 0x95F604;
inline constexpr std::uintptr_t kSamplerDestroyDescriptor = 0x962B78;
inline constexpr std::uintptr_t kModelRenderContextUnbindDefaultFrameBuffer = 0x962C18;
inline constexpr std::uintptr_t kRigidBodyBaseGetBoundingBoxWorld = 0xA3C1C4;
inline constexpr std::uintptr_t kNVNMgrRegisterTexture = 0xA5B0B4;
inline constexpr std::uintptr_t kUpdateAimCamera = 0xA6DF60;
inline constexpr std::uintptr_t kRigidBodyBaseRequestSetAngularVelocity = 0xACC65C;
inline constexpr std::uintptr_t kRigidBodyBaseRequestSetLinearVelocity = 0xACC874;
inline constexpr std::uintptr_t kUserResourceSearchAssetCallTableByName = 0xAFD150;
inline constexpr std::uintptr_t kUserInstanceEmitImpl = 0xAFD244;
inline constexpr std::uintptr_t kActorBaseGetModel = 0xBADE58;
inline constexpr std::uintptr_t kSamplerConstruct = 0xC282F8;
inline constexpr std::uintptr_t kSpecialPowerRendererComposite = 0xC31AFC;
inline constexpr std::uintptr_t kMonochromeFilterDraw = 0xC31DDC;
inline constexpr std::uintptr_t kSpecialPowerRendererDrawMask = 0xC3257C;
inline constexpr std::uintptr_t kSamplerDestroyTexture = 0xC33230;
inline constexpr std::uintptr_t kRestoreRenderBuffers = 0xC4B7AC;
inline constexpr std::uintptr_t kModelRenderContextBindDefaultFrameBuffer = 0xC5A6FC;
inline constexpr std::uintptr_t kRigidBodyApplyLinearImpulse = 0xCF600C;
inline constexpr std::uintptr_t kSpecialPowerModuleCalc = 0xD56B00;
inline constexpr std::uintptr_t kSpecialPowerRendererPrepareExtraModels = 0xD59BD8;
inline constexpr std::uintptr_t kSpecialPowerRendererReset = 0xD59FD4;
inline constexpr std::uintptr_t kActorBaseGetControllerSet = 0xDA744C;
inline constexpr std::uintptr_t kControllerSetSetFreeze = 0xE4CFA0;
inline constexpr std::uintptr_t kRigidBodyBaseRequestChangeMotionType = 0xE8BE30;
inline constexpr std::uintptr_t kActorGetEquipment = 0xFA7F08;
inline constexpr std::uintptr_t kRigidBodyEntityGetActorLink = 0xFCE23C;
inline constexpr std::uintptr_t kSamplerApplyTexture = 0xFDF050;
inline constexpr std::uintptr_t kGetAnimationComponent = 0x14E2EB8;
inline constexpr std::uintptr_t kControllerSetSetUnfreeze = 0x157BB14;
inline constexpr std::uintptr_t kUIShortCutMainItemScreenDoOutBefore = 0x19AD3F4;
inline constexpr std::uintptr_t kUIShortCutMainItemScreenDoOutBeforeImpl = 0x1B1EEF8;
inline constexpr std::uintptr_t kNativeAbilityAdmission = 0x932610;
inline constexpr std::uintptr_t kUIShortCutMainItemScreenOpenStartImpl = 0x1B1DC94;
inline constexpr std::uintptr_t kUIShortCutMainItemScreenUpdateImpl = 0x1B1E65C;
inline constexpr std::uintptr_t kUIShortCutMainItemScreenCloseEndImpl = 0x1B1EEB8;
inline constexpr std::uintptr_t kEventCallbackSLinkReplaceAssetInfo = 0x28402E0;
inline constexpr std::uintptr_t kNinJoyNpadDeviceCalc = 0x2A267BC;
// Global slots; each holds a pointer to the named object.
inline constexpr std::uintptr_t kRenderManagerSlot = 0x462E188;
inline constexpr std::uintptr_t kNvnManagerSlot = 0x462EF90;
inline constexpr std::uintptr_t kNvnGetProcAddress = 0x46707E8;
inline constexpr std::uintptr_t kGraphicsDevice = 0x4716E48;
inline constexpr std::uintptr_t kUniformAllocator = 0x4717048;
inline constexpr std::uintptr_t kSoundSystem = 0x471A578;
inline constexpr std::uintptr_t kPhysicsSystem = 0x471DCB0;
} // namespace stasis::offsets
