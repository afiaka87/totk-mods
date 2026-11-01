#pragma once

#include "GameProfiles.hpp"

namespace zonai_ascend::remote::profile121 {

inline constexpr std::ptrdiff_t kFloorQuery = 0x175D0B4;
inline constexpr std::ptrdiff_t kLayersQuery = 0xE6E070;
inline constexpr std::ptrdiff_t kVolumeStatsSlot = 0x462E3E0;
inline constexpr std::ptrdiff_t kBlockedExit = 0x175E69C;
inline constexpr std::ptrdiff_t kUnderground = 0x175E24C;
inline constexpr std::ptrdiff_t kTravelResult = 0x1C56058;
inline constexpr std::ptrdiff_t kCommitFloor = 0x1D537BC;
inline constexpr std::ptrdiff_t kCancelInput = 0x215B158;
inline constexpr std::ptrdiff_t kSelectorChild = 0x215B284;
inline constexpr std::ptrdiff_t kForwardSpeedAfterCap = 0xBF8BD0;
inline constexpr std::ptrdiff_t kApproachLookahead = 0x1C647A4;
inline constexpr std::ptrdiff_t kTravelSpeed = 0x1C56500;
inline constexpr std::ptrdiff_t kApproachPhysics = 0x1C64B08;
inline constexpr std::ptrdiff_t kTravelCommandReturn = 0x1C566E8;
inline constexpr std::ptrdiff_t kGetActor = 0xBC7610;
inline constexpr std::ptrdiff_t kGetPlayer = 0x107E9A0;
inline constexpr std::ptrdiff_t kSnapFloor = 0x1728A30;
inline constexpr std::array<std::ptrdiff_t, 3> kVoxelLimits{
    0x1B2F6C0, 0x1B2F71C, 0x1B2F76C};

// Stage-zonai-ascend 2026-10-31: exact 1.2.1 words and verified native ABI.
inline constexpr std::array<profiles::WordSite, 20> kGuards{{
    {kFloorQuery, 0xD10283FF}, {kLayersQuery, 0xA9BC7BFD},
    {kBlockedExit, 0xD100C3FF}, {kUnderground, 0xA9BF7BFD},
    {kTravelResult, 0xA9BE7BFD}, {kGetActor, 0xF9400408},
    {kCommitFloor, 0xF9400C08},
    {kCancelInput, 0xA9BD7BFD}, {kSelectorChild, 0x39407809},
    {kForwardSpeedAfterCap, 0x2D400420}, {kApproachLookahead, 0xD000DD68},
    {kTravelSpeed, 0xA9BE7BFD},
    {kApproachPhysics, 0xA9BC7BFD}, {kTravelCommandReturn-4, 0x97BE892F},
    {kGetPlayer, 0xF9400408}, {0x1728B6C, 0x97C4BE89},
    {0x175D0DC, 0xF9400117},
    {kVoxelLimits[0], 0x52817704}, {kVoxelLimits[1], 0x52817704},
    {kVoxelLimits[2], 0x52817704},
}};

template<class Read> bool matches(std::size_t textSize, Read read) {
    for (const auto& site : kGuards)
        if (static_cast<std::size_t>(site.offset)+4 > textSize ||
            read(site.offset) != site.original) return false;
    return true;
}

}
