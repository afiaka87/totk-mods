#pragma once

#include "totk/core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace totk::engine {

inline constexpr std::uintptr_t kMinimumMappedAddress = 0x1000;
inline constexpr std::uintptr_t kMaximumMappedAddress = 1ULL << 40;

[[nodiscard]] constexpr bool isPlausibleAddress(std::uintptr_t address) {
    return address >= kMinimumMappedAddress && address < kMaximumMappedAddress &&
           (address & (alignof(std::uintptr_t) - 1)) == 0;
}

[[nodiscard]] constexpr bool isPlausibleStringAddress(std::uintptr_t address) {
    return address >= kMinimumMappedAddress && address < kMaximumMappedAddress;
}

template <class Value>
[[nodiscard]] inline Value readMemory(std::uintptr_t address) {
    Value value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(Value));
    return value;
}

template <class Value>
inline void writeMemory(std::uintptr_t address, const Value& value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(Value));
}

struct Totk121Offsets {
    static constexpr const char* kGameVersion = "1.2.1";
    static constexpr const char* kBuildId = "9B4E43650501A4D4";

    static constexpr core::ImageOffset kSceneModuleInstance{0x04728538};

    static constexpr core::ImageOffset kNpadCalc{0x02A267BC};
    static constexpr core::ImageOffset kRaycastWorker{0x00858590};

    static constexpr core::ImageOffset kForceSetMatrix{0x006BA86C};

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

inline constexpr std::size_t kNpadSlotCount = 9;
inline constexpr std::ptrdiff_t kNpadSlotStride = 0xE98;
inline constexpr std::ptrdiff_t kNpadState = 0x58;
inline constexpr std::ptrdiff_t kNpadSamplingNumber = 0x00;
inline constexpr std::ptrdiff_t kNpadButtons = 0x08;
inline constexpr std::ptrdiff_t kNpadLeftStickX = 0x10;
inline constexpr std::ptrdiff_t kNpadLeftStickY = 0x14;

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

}

enum class HandleStatus : std::uint8_t {
    Current,
    NullAddress,
    SceneChanged,
    IdentityChanged,
};

struct ActorHandle {
    std::uintptr_t address = 0;
    std::uintptr_t capturedNamePointer = 0;
    core::SceneToken scene{};

    [[nodiscard]] HandleStatus status(core::SceneToken currentScene) const {
        if (!isPlausibleAddress(address)) return HandleStatus::NullAddress;
        if (scene != currentScene) return HandleStatus::SceneChanged;
        const auto currentName =
            readMemory<std::uintptr_t>(address + layout::kActorNamePointer);
        return currentName == capturedNamePointer ? HandleStatus::Current
                                                  : HandleStatus::IdentityChanged;
    }

    [[nodiscard]] bool isCurrent(core::SceneToken currentScene) const {
        return status(currentScene) == HandleStatus::Current;
    }
};

}
