
#pragma once

#include "totk/core/Units.hpp"

#include <cstddef>

namespace totk::engine {

struct Totk121Offsets {
    static constexpr const char* kGameVersion = "1.2.1";
    static constexpr const char* kBuildId = "9B4E43650501A4D4";

    static constexpr core::ImageOffset kSceneModuleInstance{0x04728538};
    static constexpr core::ImageOffset kProcessManagerIndirect{0x0462DED8};
    static constexpr core::ImageOffset kProcessListLock{0x02B17270};
    static constexpr core::ImageOffset kProcessListUnlock{0x02B17280};

    static constexpr core::ImageOffset kNpadCalc{0x02A267BC};
    static constexpr core::ImageOffset kRaycastWorker{0x00858590};

    static constexpr core::ImageOffset kForceSetMatrix{0x006BA86C};
    static constexpr core::ImageOffset kGetMotionType{0x006AB438};
    static constexpr core::ImageOffset kRequestChangeMotionType{0x00E8BE30};
    static constexpr core::ImageOffset kRequestSetLinearVelocity{0x00ACC874};

    static constexpr core::ImageOffset kGameDataManagerIndirect{0x0462E3D8};
    static constexpr core::ImageOffset kGameDataGetInt{0x010CD5BC};
    static constexpr core::ImageOffset kGameDataSetInt{0x00B51B08};
};

namespace layout {

inline constexpr std::ptrdiff_t kSceneFromModule = 0x1E8;
inline constexpr std::ptrdiff_t kSceneComponents = 0x58;
inline constexpr std::size_t kResidentActorComponentIndex = 13;

inline constexpr std::ptrdiff_t kResidentCount = 0x20;
inline constexpr std::ptrdiff_t kResidentList = 0x28;
inline constexpr std::ptrdiff_t kResidentDescriptorStride = 0x70;
inline constexpr std::ptrdiff_t kResidentDescriptor = 0x08;
inline constexpr std::ptrdiff_t kActorFromDescriptor = 0x40;

inline constexpr std::ptrdiff_t kActorNamePointer = 0x218;
inline constexpr std::ptrdiff_t kActorComponentRegistry = 0x228;
inline constexpr std::ptrdiff_t kActorPosition = 0x2B4;
inline constexpr std::ptrdiff_t kActorRotation = 0x2C0;
inline constexpr std::ptrdiff_t kActorLinearVelocity = 0x320;

inline constexpr std::ptrdiff_t kPhysicsFromRegistry = 0x50;
inline constexpr std::ptrdiff_t kRigidBodySetFromPhysics = 0x20;
inline constexpr std::ptrdiff_t kRigidBodyFromSet = 0x150;

inline constexpr std::ptrdiff_t kProcessListMutex = 72;
inline constexpr std::ptrdiff_t kProcessActiveHead = 112;
inline constexpr std::ptrdiff_t kProcessActiveLinkOffset = 124;
inline constexpr std::ptrdiff_t kProcessName = 24;
inline constexpr std::ptrdiff_t kProcessState = 36;

inline constexpr std::size_t kNpadSlotCount = 9;
inline constexpr std::ptrdiff_t kNpadSlotStride = 0xE98;
inline constexpr std::ptrdiff_t kNpadState = 0x58;
inline constexpr std::ptrdiff_t kNpadSamplingNumber = 0x00;
inline constexpr std::ptrdiff_t kNpadButtons = 0x08;
inline constexpr std::ptrdiff_t kNpadLeftStickX = 0x10;
inline constexpr std::ptrdiff_t kNpadLeftStickY = 0x14;
inline constexpr std::ptrdiff_t kNpadRightStickX = 0x18;
inline constexpr std::ptrdiff_t kNpadRightStickY = 0x1c;

inline constexpr std::size_t kRaycastObjectSize = 0x200;
inline constexpr std::ptrdiff_t kRaycastHit = 0x20;
inline constexpr std::ptrdiff_t kRaycastPosition = 0x24;
inline constexpr std::ptrdiff_t kRaycastNormal = 0x30;
inline constexpr std::ptrdiff_t kRaycastDistance = 0x40;

inline constexpr std::ptrdiff_t kGameDataIntStore = 200;
inline constexpr std::ptrdiff_t kGameDataEnumStore = 680;
inline constexpr std::ptrdiff_t kGameDataVector2Store = 872;
inline constexpr std::ptrdiff_t kGameDataQueueCapacity = 40;
inline constexpr std::ptrdiff_t kGameDataQueueBuffer = 48;
inline constexpr std::ptrdiff_t kGameDataQueueControl = 56;
inline constexpr std::uint32_t kGameDataQueueIndexMask = 0xFFFFF;
inline constexpr std::int32_t kGameDataQueueHeadroom = 2;

} // namespace layout

} // namespace totk::engine
