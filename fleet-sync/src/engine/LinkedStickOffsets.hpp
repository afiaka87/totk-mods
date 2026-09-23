#pragma once

#include <cstddef>

// Version-locked to TotK 1.2.1; other builds must fail the installation guards.
namespace linked_stick::engine::offsets {

inline constexpr std::ptrdiff_t kNpadCalc = 0x02A267BC;
inline constexpr std::ptrdiff_t kGetControlStickX = 0x01B6F19C;
inline constexpr std::ptrdiff_t kGetControlStickForwardBack = 0x01B6F1F8;
inline constexpr std::ptrdiff_t kGetControlStickY = 0x01B6F21C;
inline constexpr std::ptrdiff_t kIsSpecialPartsOn = 0x0161D1E4;
inline constexpr std::ptrdiff_t kSetActiveControlStick = 0x013C54E4;
inline constexpr std::ptrdiff_t kReleaseActiveControlStick = 0x013C5528;
inline constexpr std::ptrdiff_t kEvaluateRidableIsRiddenSeat = 0x00B5E200;
inline constexpr std::ptrdiff_t kRidableGetRiderType = 0x00B5E46C;
inline constexpr std::ptrdiff_t kCopyRiderInput = 0x00B7FBD8;
inline constexpr std::ptrdiff_t kRidableSeatGetRiderType = 0x00B5E490;
inline constexpr std::ptrdiff_t kOneShotBaseGetActor = 0x00C545DC;
inline constexpr std::ptrdiff_t kRideOnCombinedControlStickExecute =
    0x01F16520;
inline constexpr std::ptrdiff_t kRemoteEnergyRangeLoad = 0x00684C48;
inline constexpr std::ptrdiff_t kActorPresenceIsCalc = 0x0140F2E8;
inline constexpr std::ptrdiff_t kActorPresenceIsUnload = 0x014103C4;
inline constexpr std::ptrdiff_t kActorPresenceIsDelete = 0x00B9297C;
inline constexpr std::ptrdiff_t kGetTotalMassAndInertia = 0x006AB474;
inline constexpr std::ptrdiff_t kActorLinkGetReference = 0x00753530;
inline constexpr std::ptrdiff_t kBodyNextLinearVelocity = 0x011B44AC;
inline constexpr std::ptrdiff_t kBodyNextAngularVelocity = 0x00658AB4;
inline constexpr std::ptrdiff_t kBodyCenterOfMassWorld = 0x0085C240;
inline constexpr std::ptrdiff_t kRequestAngularVelocity = 0x00ACC65C;
inline constexpr std::ptrdiff_t kBodySdkInstance = 0x70;
inline constexpr std::ptrdiff_t kPhysicsProcessRequests = 0x00AA6ED8;
inline constexpr std::ptrdiff_t kPhysicsStepSeconds = 0x24;
inline constexpr std::ptrdiff_t kPhysicsMode = 0x5C;
inline constexpr std::ptrdiff_t kPhysicsModeValid = 0x60;
inline constexpr std::ptrdiff_t kPhysicsWorldIndex = 0x64;
inline constexpr std::ptrdiff_t kPhysicsWorldIndexValid = 0x68;
inline constexpr std::ptrdiff_t kPhysicsEntityWorld = 0xC0;
inline constexpr std::ptrdiff_t kWorldHavok = 0xE0;
inline constexpr std::ptrdiff_t kHavokReadVelocitiesVtable = 0x1C8;
inline constexpr std::ptrdiff_t kHavokReadVelocities = 0x0015144C;
inline constexpr std::ptrdiff_t kBodyFlags = 0x68;
inline constexpr std::ptrdiff_t kBodyChangeRequest = 0x60;
inline constexpr std::ptrdiff_t kChangeRequestFlags = 0xD4;
inline constexpr std::ptrdiff_t kSdkBodyId = 0x8;
inline constexpr std::ptrdiff_t kRequestLayerHitMask = 0x00ED9A98;
inline constexpr std::ptrdiff_t kHavokBodyIsValid = 0x001D1248;
inline constexpr std::ptrdiff_t kHavokGetBody = 0x001D14D8;
inline constexpr std::ptrdiff_t kHavokBodyPhive = 0xB8;
inline constexpr std::ptrdiff_t kHavokBodyMotionId = 0x80;
inline constexpr std::ptrdiff_t kBodyLayerHitMask = 0x11C;
inline constexpr std::ptrdiff_t kBodyCollisionFilter = 0x140;
inline constexpr std::ptrdiff_t kFilterLayerHitMask = 0xC;
inline constexpr std::ptrdiff_t kPendingLayerAdd = 0x94;
inline constexpr std::ptrdiff_t kPendingLayerRemove = 0x98;

inline constexpr std::ptrdiff_t kRideOnCombinedControlStickVtable =
    0x044CD098;

inline constexpr std::ptrdiff_t kComponentRegistry = 0x228;
inline constexpr std::ptrdiff_t kSpecialPowerReceiver = 0x480;
inline constexpr std::ptrdiff_t kRidable = 0x408;
inline constexpr std::ptrdiff_t kComponentOwnerActor = 0x18;
inline constexpr std::ptrdiff_t kZonauGearEnergyRange = 0x48;
inline constexpr std::ptrdiff_t kCombinedActorIntegrator = 0x28;
inline constexpr std::ptrdiff_t kCombinedActorIntegratorIndex = 0x20;
inline constexpr std::ptrdiff_t kCombinedActorMemberCount = 0x58;
inline constexpr std::ptrdiff_t kCombinedActorMemberLinks = 0x60;
inline constexpr std::ptrdiff_t kCombinedActorCachedMass = 0x88C;
inline constexpr std::ptrdiff_t kCombinedActorCenterOfMass = 0x89C;
inline constexpr std::ptrdiff_t kCombinedActorRotation = 0x8B4;
inline constexpr std::ptrdiff_t kSpecialPowerReceiverFlags = 0x20;
inline constexpr std::uint64_t kImaginaryAutobuildActorFlag = 0x10000000;
inline constexpr std::ptrdiff_t kRidableSeatCount = 0x20;
inline constexpr std::ptrdiff_t kRidableSeatArray = 0x28;
inline constexpr std::ptrdiff_t kPreActorLiveActor = 0x20;
inline constexpr std::ptrdiff_t kActorReferenceCount = 0x1B0;

}
