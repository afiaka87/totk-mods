// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstddef>
#if defined(TOTK_PORT)
#include "totk/port/Port.hpp"
#endif
namespace cryonis::layout {
namespace wheel {
inline constexpr std::size_t kCount = 0x1A8, kCursor = 0x23C, kBusy = 0x244, kHighlight = 0x245,
                             kIds = 0x270;
inline constexpr unsigned kAmiibo = 8;
}
namespace prompt {
inline constexpr std::size_t kLayout = 0x28, kRoot = 0x18, kFlags = 0x58;
}
namespace actor {
inline constexpr std::size_t kComponents = 0x228, kPhysics = 0x50, kControllerSet = 0x20,
                             kRotation = 0x2C0, kDynamicEquipment = 0x208;
}
namespace physics {
inline constexpr std::size_t kTime = 0xC8, kPausedFlags = 0x18;
}
namespace node {
inline constexpr std::size_t kOutputs = 40, kState = 32, kFlags = 24;
}
namespace camera {
#if defined(TOTK_PORT)
// The camera component grew by 72 bytes ahead of both members on 1.4.3.
inline constexpr totk::port::Member kState{1096, 1168}, kTarget{3144, 3216};
inline constexpr std::size_t kTargetActor = 24;
#else
inline constexpr std::size_t kState = 1096, kTarget = 3144, kTargetActor = 24;
#endif
}
}
