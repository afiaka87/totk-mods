// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "Totk121Offsets.hpp"
#include "totk/engine/Pointer.hpp"
#include <lib.hpp>
#include <span>
namespace stasis::signatures {
// The first two instructions of a TotK 1.2.1 function; a zero second word checks only the first.
struct Site {
    std::uintptr_t offset;
    std::uint32_t first, second;
};
inline constexpr Site kRuntime[]{
    {offsets::kPhysicsModuleUpdateDeltaFrame, 0xF9400828, 0xB40002A8},
    {offsets::kTgHitInfoSetHitInfo, 0xD102C3FF, 0xFD002BEA},
    {offsets::kRigidBodyGetCenterOfMass, 0xD100C3FF, 0xA9017BFD},
    {offsets::kNinJoyNpadDeviceCalc, 0xD102C3FF, 0xA9057BFD},
    {offsets::kActorBaseGetControllerSet, 0xF9411408, 0xB40000A8},
    {offsets::kControllerSetSetFreeze, 0xF940AC00, 0xB4000040},
    {offsets::kControllerSetSetUnfreeze, 0xF940AC08, 0xB4000248},
    {offsets::kRigidBodyApplyLinearImpulse, 0xD10143FF, 0xFD000BE8},
    {offsets::kRigidBodyBaseRequestSetLinearVelocity, 0xFC1B0FEA, 0x6D0123E9},
    {offsets::kRigidBodyBaseRequestSetAngularVelocity, 0xFC1B0FEA, 0x6D0123E9},
    {offsets::kAiElementDoUpdate, 0xA9BB7BFD, 0xF9000BF9},
    {offsets::kAsControllerUpdate, 0xD10503FF, 0x6D0D23E9},
    {offsets::kGamePhysicsPreCalc, 0xD10283FF, 0x6D052BEB},
    {offsets::kActorBaseUpdatePositionAndRotation, 0xD103C3FF, 0xFD0033EC},
    {offsets::kActorSetPose, 0xD10203FF, 0xFD0023EA},
    {offsets::kActorBaseForceSetMatrix, 0xA9BD7BFD, 0xF9000BF5},
    {offsets::kGetAnimationComponent, 0xF9411408, 0xB4000068},
    {offsets::kRayCast, 0xD10743FF, 0xA9187BFD},
    {offsets::kRigidBodyBaseGetBoundingBoxWorld, 0xD10243FF, 0xA9067BFD},
    {offsets::kControllerSetVisitRigidBodies, 0xD10183FF, 0xA9017BFD},
    {offsets::kRigidBodyBaseRequestChangeMotionType, 0xA9BD7BFD, 0xF9000BF5},
    {offsets::kGetDynamicEquipment, 0xF9411408, 0xB4000068},
    {offsets::kDynamicEquipmentGetUser, 0xA9BC7BFD, 0xF9000BF7},
    {offsets::kActorReferenceGetSystemGroupID, 0xF9400009, 0xB4000189},
    {offsets::kRigidBodyEntityGetActorLink, 0xF9408808, 0x9001B30A},
    {offsets::kActorLinkBaseGetReference, 0xA9BD7BFD, 0xA90157F6},
    {offsets::kSpecialPowerModuleCalc, 0x6DB63BEF, 0x6D0133ED},
    {offsets::kSpecialPowerRendererReset, 0xA9BA7BFD, 0xA9016FFC},
    {offsets::kSpecialPowerRendererPrepareExtraModels, 0xA9BA7BFD, 0xA9016FFC},
    {offsets::kSpecialPowerRendererDrawMask, 0xA9BA7BFD, 0xA9016FFC},
    {offsets::kSpecialPowerRendererComposite, 0xA9BA7BFD, 0xA9016FFC},
    {offsets::kMonochromeFilterDraw, 0xD10643FF, 0x6D112BEB},
    {offsets::kActorBaseGetModel, 0xF9411408, 0xB40000C8},
    {offsets::kSamplerApplyTexture, 0xD10183FF, 0xA9027BFD},
    {offsets::kSamplerConstruct, 0xD10183FF, 0xA9027BFD},
    {offsets::kSamplerDestroyDescriptor, 0xA9BD7BFD, 0xF9000BF5},
    {offsets::kSamplerDestroyTexture, 0xA9BD7BFD, 0xA90157F6},
    {offsets::kUIShortCutMainItemScreenOpenStartImpl, 0xD10283FF, 0xFD001BE8},
    {offsets::kUIShortCutMainItemScreenUpdateImpl, 0xD10203FF, 0x6D032BEB},
    {offsets::kUIShortCutMainItemScreenDoOutBefore, 0xA9BE7BFD, 0xA9014FF4},
    {offsets::kUIShortCutMainItemScreenCloseEndImpl, 0xA9BE7BFD, 0xA9014FF4},
    {offsets::kUpdateAimCamera, 0xD102C3FF, 0x6D033BEF}};
inline constexpr Site kAudio[]{
    {offsets::kEventCallbackSLinkReplaceAssetInfo, 0xA9BD7BFD, 0xA90157F6},
    {offsets::kUserResourceSearchAssetCallTableByName, 0xA9BA7BFD, 0},
    {offsets::kUserInstanceEmitImpl, 0xA9BA7BFD, 0}};
// True when every site matches the running game; the first mismatch is logged under tag.
inline bool codeMatches(std::uintptr_t base, std::span<const Site> sites, const char *tag) {
    for (const auto site : sites) {
        const auto first = totk::engine::readMemory<std::uint32_t>(base + site.offset);
        const auto second = totk::engine::readMemory<std::uint32_t>(base + site.offset + 4);
        if (first != site.first || (site.second && second != site.second)) {
            Logging.Log("[stasis] %s offset=%p expected=%08x,%08x actual=%08x,%08x", tag,
                        reinterpret_cast<void *>(site.offset), site.first, site.second, first,
                        second);
            return false;
        }
    }
    return true;
}
} // namespace stasis::signatures
