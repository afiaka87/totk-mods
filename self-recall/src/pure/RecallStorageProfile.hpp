#pragma once

#ifndef SELF_RECALL_STORAGE_PROFILE
#define SELF_RECALL_STORAGE_PROFILE 0
#endif

namespace self_recall::pure {

inline constexpr unsigned kMiB = 1024u * 1024u;

#if SELF_RECALL_STORAGE_PROFILE == 0
inline constexpr const char* kStorageProfileName = "full";
inline constexpr unsigned kPosePayloadArenaBytes = 72u * kMiB;
inline constexpr unsigned kAppearancePoolBytes = 16u * kMiB;
inline constexpr unsigned kArchiveHeapBytes = 16u * kMiB;
#elif SELF_RECALL_STORAGE_PROFILE == 1
inline constexpr const char* kStorageProfileName = "switch-balanced";
inline constexpr unsigned kPosePayloadArenaBytes = 52u * kMiB;
inline constexpr unsigned kAppearancePoolBytes = 12u * kMiB;
inline constexpr unsigned kArchiveHeapBytes = 8u * kMiB;
#elif SELF_RECALL_STORAGE_PROFILE == 2
inline constexpr const char* kStorageProfileName = "hardware-canary";
inline constexpr unsigned kPosePayloadArenaBytes = 8u * kMiB;
inline constexpr unsigned kAppearancePoolBytes = 2u * kMiB;
inline constexpr unsigned kArchiveHeapBytes = 2u * kMiB;
#elif SELF_RECALL_STORAGE_PROFILE == 3
inline constexpr const char* kStorageProfileName = "switch-midpoint";
inline constexpr unsigned kPosePayloadArenaBytes = 32u * kMiB;
inline constexpr unsigned kAppearancePoolBytes = 6u * kMiB;
inline constexpr unsigned kArchiveHeapBytes = 4u * kMiB;
#elif SELF_RECALL_STORAGE_PROFILE == 4
inline constexpr const char* kStorageProfileName = "switch-low";
inline constexpr unsigned kPosePayloadArenaBytes = 20u * kMiB;
inline constexpr unsigned kAppearancePoolBytes = 4u * kMiB;
inline constexpr unsigned kArchiveHeapBytes = 3u * kMiB;
#elif SELF_RECALL_STORAGE_PROFILE == 5
inline constexpr const char* kStorageProfileName = "switch-quarter";
inline constexpr unsigned kPosePayloadArenaBytes = 29u * kMiB / 2u;
inline constexpr unsigned kAppearancePoolBytes = 3u * kMiB;
inline constexpr unsigned kArchiveHeapBytes = 2u * kMiB;
#elif SELF_RECALL_STORAGE_PROFILE == 6
inline constexpr const char* kStorageProfileName = "lossless-evaluation";
inline constexpr unsigned kPosePayloadArenaBytes = 32u * kMiB;
inline constexpr unsigned kAppearancePoolBytes = 8u * kMiB;
inline constexpr unsigned kArchiveHeapBytes = 16u * kMiB;
#elif SELF_RECALL_STORAGE_PROFILE == 7
inline constexpr const char* kStorageProfileName = "switch-compressed";
inline constexpr unsigned kPosePayloadArenaBytes = 10u * kMiB;
inline constexpr unsigned kAppearancePoolBytes = 5u * kMiB / 4u;
inline constexpr unsigned kArchiveHeapBytes = 2u * kMiB;
#else
#error "Unknown SELF_RECALL_STORAGE_PROFILE"
#endif

inline constexpr bool kCompressedStorage = SELF_RECALL_STORAGE_PROFILE == 7;
inline constexpr bool kLosslessStorage = SELF_RECALL_STORAGE_PROFILE == 6 || kCompressedStorage;
inline constexpr unsigned kHistoryFrameCapacity = kLosslessStorage ? 902u : 3840u;
inline constexpr unsigned kHistorySeconds = kLosslessStorage ? 30u : 64u;

inline constexpr unsigned kAppearanceBlockBytes = 256u;
inline constexpr unsigned kAppearanceBlockCount = kAppearancePoolBytes / kAppearanceBlockBytes;
inline constexpr unsigned kAppearanceStateCapacity = kAppearanceBlockCount;

static_assert(kPosePayloadArenaBytes % 1024u == 0);
static_assert(kAppearancePoolBytes % kAppearanceBlockBytes == 0);
static_assert(kArchiveHeapBytes % 4096u == 0);

}  // namespace self_recall::pure
