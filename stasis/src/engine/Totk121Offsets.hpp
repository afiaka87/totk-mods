// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "totk/port/Port.hpp"
#include <cstdint>
namespace stasis::offsets {
// TotK 1.2.1 function offsets from the module base; each gives the running game's offset where used.
inline constexpr totk::port::Code kGamePhysicsPreCalc = 0x6359F4;
inline constexpr totk::port::Code kLifePreCalc = 0x64B264;
inline constexpr totk::port::Code kLifeCalcDamage = 0x9DF274;
inline constexpr totk::port::Code kRigidBodyEntityGetLinearVelocity = 0xACFC80;
inline constexpr totk::port::Code kGameActorIsProjectile = 0x158B940;
inline constexpr totk::port::Code kAsControllerUpdate = 0x6437BC;
inline constexpr totk::port::Code kDynamicEquipmentGetUser = 0x666E24;
inline constexpr totk::port::Code kGetDynamicEquipment = 0x6A7BC0;
inline constexpr totk::port::Code kGetStaticEquipment = 0x15BE08C;
inline constexpr totk::port::Code kStaticEquipmentGetUser = 0xE4A510;
inline constexpr totk::port::Code kGetAttachment = 0x67B6B0;
inline constexpr totk::port::Code kAttachmentIsValidOwner = 0x6C3FB0;
inline constexpr totk::port::Code kRigidBodyBaseGetMotionType = 0x6AB438;
inline constexpr totk::port::Code kActorBaseForceSetMatrix = 0x6BA86C;
inline constexpr totk::port::Code kActorLinkBaseGetReference = 0x6CB510;
inline constexpr totk::port::Code kGraphicsContextGraphicsContext = 0x74C19C;
inline constexpr totk::port::Code kAiElementDoUpdate = 0x74FF2C;
inline constexpr totk::port::Code kGraphicsContextApply = 0x756A08;
inline constexpr totk::port::Code kPhysicsModuleUpdateDeltaFrame = 0x7EDC00;
inline constexpr totk::port::Code kDynamicTextureAllocatorFree = 0x7EF80C;
inline constexpr totk::port::Code kActorBaseUpdatePositionAndRotation = 0x823FD8;
inline constexpr totk::port::Code kActorReferenceGetSystemGroupID = 0x84E674;
inline constexpr totk::port::Code kRayCast = 0x858590;
inline constexpr totk::port::Code kRigidBodyGetCenterOfMass = 0x85C240;
inline constexpr totk::port::Code kActorSetPose = 0x860020;
inline constexpr totk::port::Code kTgHitInfoSetHitInfo = 0x8638B8;
inline constexpr totk::port::Code kControllerSetVisitRigidBodies = 0x8AA374;
inline constexpr totk::port::Code kShaderProgramActivateSampler = 0x95EF6C;
inline constexpr totk::port::Code kUtlCreateDynamicUniformBlock = 0x95F604;
inline constexpr totk::port::Code kSamplerDestroyDescriptor = 0x962B78;
inline constexpr totk::port::Code kModelRenderContextUnbindDefaultFrameBuffer = 0x962C18;
inline constexpr totk::port::Code kRigidBodyBaseGetBoundingBoxWorld = 0xA3C1C4;
inline constexpr totk::port::Code kNVNMgrRegisterTexture = 0xA5B0B4;
inline constexpr totk::port::Code kUpdateAimCamera = 0xA6DF60;
inline constexpr totk::port::Code kRigidBodyBaseRequestSetAngularVelocity = 0xACC65C;
inline constexpr totk::port::Code kRigidBodyBaseRequestSetLinearVelocity = 0xACC874;
inline constexpr totk::port::Code kUserResourceSearchAssetCallTableByName = 0xAFD150;
inline constexpr totk::port::Code kUserInstanceEmitImpl = 0xAFD244;
inline constexpr totk::port::Code kActorBaseGetModel = 0xBADE58;
inline constexpr totk::port::Code kSamplerConstruct = 0xC282F8;
inline constexpr totk::port::Code kSpecialPowerRendererComposite = 0xC31AFC;
inline constexpr totk::port::Code kMonochromeFilterDraw = 0xC31DDC;
inline constexpr totk::port::Code kSpecialPowerRendererDrawMask = 0xC3257C;
inline constexpr totk::port::Code kSamplerDestroyTexture = 0xC33230;
inline constexpr totk::port::Code kRestoreRenderBuffers = 0xC4B7AC;
inline constexpr totk::port::Code kModelRenderContextBindDefaultFrameBuffer = 0xC5A6FC;
inline constexpr totk::port::Code kRigidBodyApplyLinearImpulse = 0xCF600C;
inline constexpr totk::port::Code kSpecialPowerModuleCalc = 0xD56B00;
inline constexpr totk::port::Code kSpecialPowerRendererPrepareExtraModels = 0xD59BD8;
inline constexpr totk::port::Code kSpecialPowerRendererReset = 0xD59FD4;
inline constexpr totk::port::Code kActorBaseGetControllerSet = 0xDA744C;
inline constexpr totk::port::Code kControllerSetSetFreeze = 0xE4CFA0;
inline constexpr totk::port::Code kRigidBodyBaseRequestChangeMotionType = 0xE8BE30;
inline constexpr totk::port::Code kActorGetEquipment = 0xFA7F08;
inline constexpr totk::port::Code kRigidBodyEntityGetActorLink = 0xFCE23C;
inline constexpr totk::port::Code kSamplerApplyTexture = 0xFDF050;
inline constexpr totk::port::Code kGetAnimationComponent = 0x14E2EB8;
inline constexpr totk::port::Code kControllerSetSetUnfreeze = 0x157BB14;
inline constexpr totk::port::Code kUIShortCutMainItemScreenDoOutBefore = 0x19AD3F4;
inline constexpr totk::port::Code kUIShortCutMainItemScreenDoOutBeforeImpl = 0x1B1EEF8;
inline constexpr totk::port::Code kNativeAbilityAdmission = 0x932610;
inline constexpr totk::port::Code kUIShortCutMainItemScreenOpenStartImpl = 0x1B1DC94;
inline constexpr totk::port::Code kUIShortCutMainItemScreenUpdateImpl = 0x1B1E65C;
inline constexpr totk::port::Code kUIShortCutMainItemScreenCloseEndImpl = 0x1B1EEB8;
inline constexpr totk::port::Code kEventCallbackSLinkReplaceAssetInfo = 0x28402E0;
inline constexpr totk::port::Code kNinJoyNpadDeviceCalc = 0x2A267BC;
// Global slots; each holds a pointer to the named object.
inline constexpr totk::port::Code kRenderManagerSlot = 0x462E188;
inline constexpr totk::port::Code kDynamicTextureAllocatorSlot = 0x4638208;
inline constexpr totk::port::Code kModelNWTypeSlot = 0x462EF10;
inline constexpr totk::port::Code kNvnManagerSlot = 0x462EF90;
inline constexpr totk::port::Code kNvnGetProcAddress = 0x46707E8;
inline constexpr totk::port::Code kGraphicsDevice = 0x4716E48;
inline constexpr totk::port::Code kUniformAllocator = 0x4717048;
inline constexpr totk::port::Code kSoundSystem = 0x471A578;
inline constexpr totk::port::Code kPhysicsSystem = 0x471DCB0;
} // namespace stasis::offsets
