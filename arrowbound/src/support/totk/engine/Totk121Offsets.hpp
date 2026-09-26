// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include "totk/core/Units.hpp"

#include <cstddef>

namespace totk::engine {
// Game addresses default to 1.2.1's; ActiveGame.cpp replaces each from the running build's profile at startup.
struct Totk121Offsets {
    // SceneModule singleton variable; one dereference.
    static inline core::ImageOffset kSceneModuleInstance{0x04728538};
    static inline core::ImageOffset kForceSetMatrix{0x006BA86C};
    // Zero (unavailable) off 1.2.1; the helpers that use them check for that.
    static inline core::ImageOffset kGetMotionType{0x006AB438};
    static inline core::ImageOffset kRequestChangeMotionType{0x00E8BE30};
    static inline core::ImageOffset kRequestSetLinearVelocity{0x00ACC874};
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

// Per build: 1.0.0 actor fields sit 8 bytes lower.
inline std::ptrdiff_t kActorNamePointer = 0x218;
inline std::ptrdiff_t kActorComponentRegistry = 0x228;
inline std::ptrdiff_t kActorPosition = 0x2B4;
inline std::ptrdiff_t kActorRotation = 0x2C0;
inline std::ptrdiff_t kActorLinearVelocity = 0x320;

inline constexpr std::ptrdiff_t kPhysicsFromRegistry = 0x50;
inline constexpr std::ptrdiff_t kRigidBodySetFromPhysics = 0x20;
inline constexpr std::ptrdiff_t kRigidBodyFromSet = 0x150;

inline constexpr std::size_t kNpadSlotCount = 9;
inline constexpr std::ptrdiff_t kNpadSlotStride = 0xE98;
inline constexpr std::ptrdiff_t kNpadState = 0x58;
inline constexpr std::ptrdiff_t kNpadSamplingNumber = 0x00;
inline constexpr std::ptrdiff_t kNpadButtons = 0x08;
inline constexpr std::ptrdiff_t kNpadLeftStickX = 0x10;
inline constexpr std::ptrdiff_t kNpadLeftStickY = 0x14;
// nn::hid::NpadBaseState layout: sampling, buttons, left stick, right stick as consecutive
// records.
inline constexpr std::ptrdiff_t kNpadRightStickX = 0x18;
inline constexpr std::ptrdiff_t kNpadRightStickY = 0x1c;

inline constexpr std::ptrdiff_t kGameDataQueueCapacity = 40;
inline constexpr std::ptrdiff_t kGameDataQueueBuffer = 48;
inline constexpr std::ptrdiff_t kGameDataQueueControl = 56;
inline constexpr std::uint32_t kGameDataQueueIndexMask = 0xFFFFF;
inline constexpr std::int32_t kGameDataQueueHeadroom = 2;

} // namespace layout

} // namespace totk::engine
